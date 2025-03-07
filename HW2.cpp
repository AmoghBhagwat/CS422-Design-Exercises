#include "pin.H"
#include "types_core.PH"
#include "types_vmapi.PH"
#include <iostream>
#include <fstream>
#include <types.h>
#include <array>
#include <vector>
using std::cerr;
using std::endl;
using std::string;
using std::array;
using std::vector;

/* ================================================================== */
// Global variables
/* ================================================================== */
VOID Fini(INT32 code, VOID* v);

typedef array<UINT64,2> DirectionPredictorData;
typedef array<UINT64,3> DirectionData; // 0 for conditional forward, 1 for conditional backward, 2 for indirect
typedef array<UINT64,2> BTBPredictorData; // mispredictions, cache miss

string directionPredictorNames[] = {
    "FNBT",
    "Bimodal",
    "SAg",
    "GAg",
    "gshare",
    "Hybrid-1",
    "Hybrid-2 Majority",
    "Hybrid-2 Tournament"
};

DirectionPredictorData directionPredictorData[8];
DirectionData directionData;
BTBPredictorData btbPredictorData[2];

INT32 bimodalPHT[512] = {0}; // 512x2 PHT

INT32 SAgBHT[1024] = {0};  // 1024x9 BHT
INT32 SAgPHT[512] = {0};  // 512x2 PHT

INT32 globalHistory = 0;  // 9 bit global history
INT32 GAgPHT[512] = {0};  // 512x3 PHT

INT32 GAgHistory = 0;  // 9 bit global history
INT32 gsharePHT[512] = {0};  // 512x3 PHT

INT32 hybrid1PHT[512] = {0};  // 512x2 PHT, indexed by global history

INT32 hybrid2_SAg_GAg[512] = {0};  // 512x2 PHT, indexed by global history
INT32 hybrid2_GAg_gshare[512] = {0};  // 512x2 PHT, indexed by global history
INT32 hybrid2_gshare_SAg[512] = {0};  // 512x2 PHT, indexed by global history

UINT64 LRUClock = 0;
struct BTBEntry {
    BOOL valid;
    ADDRINT tag;
    UINT64 LRU;
    ADDRINT target;
};

vector<vector<BTBEntry>> BTB1(128, vector<BTBEntry>(4, {false, 0, 0, 0}));
vector<vector<BTBEntry>> BTB2(128, vector<BTBEntry>(4, {false, 0, 0, 0}));

std::ostream* out = &cerr;
UINT64 fastForward = 0;

UINT64 instructionCount = 0;
ADDRINT fastForwardDone = 0;
/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");
KNOB<BOOL> KnobCount(KNOB_MODE_WRITEONCE, "pintool", "count", "1", "count instructions, basic blocks and threads in the application");
KNOB<UINT64> KnobFastForward(KNOB_MODE_WRITEONCE, "pintool", "f", "0", "fast forward to the specified instruction count");

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */
UINT32 CheckTerminate() { return instructionCount >= fastForward + 1'000'000'000; }
UINT32 CheckFastForward() { return instructionCount >= fastForward && instructionCount < fastForward + 1'000'000'000; }
VOID FastForwardDone() { fastForwardDone = 1; }
ADDRINT IsFastForwardDone() { return fastForwardDone; }

VOID CountInstruction(UINT32 count) { instructionCount += count; }
VOID UpdateDirectionPredictors(ADDRINT instructionAddress, ADDRINT branchTarget, BOOL taken) {
    INT32 branchType = (branchTarget > instructionAddress) ? 0 : 1; // 0 for forward, 1 for backward
    directionData[branchType]++;

    // FNBT
    INT32 FNBT_Mispredicted = ((branchType == 0) && taken) || ((branchType == 1) && !taken);
    directionPredictorData[0][branchType] += FNBT_Mispredicted;

    // Bimodal
    INT32 bimodalIndex = (instructionAddress & 0x1FF);
    INT32 bimodalPrediction = bimodalPHT[bimodalIndex] >= 2;
    INT32 bimodalMispredicted = bimodalPrediction != taken;
    directionPredictorData[1][branchType] += bimodalMispredicted;

    // SAg
    INT32 SAgIndex = (instructionAddress & 0x3FF);
    INT32 SAgHistory = SAgBHT[SAgIndex];
    INT32 SAgPrediction = SAgPHT[SAgHistory] >= 2;
    INT32 SAgMispredicted = SAgPrediction != taken;
    directionPredictorData[2][branchType] += SAgMispredicted;

    // GAg
    INT32 GAgPrediction = GAgPHT[globalHistory] >= 4;
    INT32 GAgMispredicted = GAgPrediction != taken;
    directionPredictorData[3][branchType] += GAgMispredicted;

    // gshare
    INT32 gshareIndex = (instructionAddress & 0x1FF) ^ globalHistory;
    INT32 gsharePrediction = gsharePHT[gshareIndex] >= 4;
    INT32 gshareMispredicted = gsharePrediction != taken;
    directionPredictorData[4][branchType] += gshareMispredicted;

    // Hybrid of SAg and GAg
    INT32 hybrid1Index = globalHistory;
    INT32 hybrid1TournamentPrediction = hybrid1PHT[hybrid1Index] >= 2;
    INT32 hybrid1Prediction = (hybrid1TournamentPrediction) ? GAgPrediction : SAgPrediction;
    INT32 hybrid1Mispredicted = hybrid1Prediction != taken;
    directionPredictorData[5][branchType] += hybrid1Mispredicted;

    // Hybrid of SAg, GAg, gshare (Majority)
    INT32 hybrid2_MajorityPrediction = (SAgPrediction + GAgPrediction + gsharePrediction) >= 2;
    INT32 hybrid2_MajorityMispredicted = hybrid2_MajorityPrediction != taken;
    directionPredictorData[6][branchType] += hybrid2_MajorityMispredicted;

    // Hybrid of SAg, GAg, gshare (Tournament)
    INT32 hybrid2Index = globalHistory;
    INT32 hybrid2_SAg_GAgPrediction = hybrid2_SAg_GAg[hybrid2Index] >= 2;
    INT32 hybrid2_TournamentPrediction;
    if (hybrid2_SAg_GAgPrediction) { // GAg
        hybrid2_TournamentPrediction = (hybrid2_GAg_gshare[hybrid2Index] >= 2) ? gsharePrediction : GAgPrediction;
    } else { // SAg
        hybrid2_TournamentPrediction = (hybrid2_gshare_SAg[hybrid2Index] >= 2) ? SAgPrediction : gsharePrediction;
    }
    INT32 hybrid2_TournamentMispredicted = hybrid2_TournamentPrediction != taken;
    directionPredictorData[7][branchType] += hybrid2_TournamentMispredicted;

    // Update all predictors
    // Update bimodal
    if (taken && bimodalPHT[bimodalIndex] < 3) {
        bimodalPHT[bimodalIndex]++;
    } else if (!taken && bimodalPHT[bimodalIndex] > 0) {
        bimodalPHT[bimodalIndex]--;
    }
    // Update SAg
    if (taken && SAgPHT[SAgHistory] < 3) {
        SAgPHT[SAgHistory]++;
    } else if (!taken && SAgPHT[SAgHistory] > 0) {
        SAgPHT[SAgHistory]--;
    }
    SAgBHT[SAgIndex] = ((SAgBHT[SAgIndex] << 1) | taken) & 0x1FF;
    // Update GAg
    if (taken && GAgPHT[globalHistory] < 7) {
        GAgPHT[globalHistory]++;
    } else if (!taken && GAgPHT[globalHistory] > 0) {
        GAgPHT[globalHistory]--;
    }
    globalHistory = ((globalHistory << 1) | taken) & 0x1FF;
    // Update gshare
    if (taken && gsharePHT[gshareIndex] < 7) {
        gsharePHT[gshareIndex]++;
    } else if (!taken && gsharePHT[gshareIndex] > 0) {
        gsharePHT[gshareIndex]--;
    }
    // Update Hybrid of SAg and GAg
    if (SAgMispredicted != GAgMispredicted) {
        if (SAgMispredicted) {
            if (hybrid1PHT[hybrid1Index] < 3) {
                hybrid1PHT[hybrid1Index]++;
            }
        } else {
            if (hybrid1PHT[hybrid1Index] > 0) {
                hybrid1PHT[hybrid1Index]--;
            }
        }
    }
    // Update Hybrid of SAg, GAg, gshare (Majority)
    if (SAgMispredicted != GAgMispredicted) {
        if (SAgMispredicted) {
            if (hybrid2_SAg_GAg[hybrid2Index] < 3) {
                hybrid2_SAg_GAg[hybrid2Index]++;
            }
        } else {
            if (hybrid2_SAg_GAg[hybrid2Index] > 0) {
                hybrid2_SAg_GAg[hybrid2Index]--;
            }
        }
    }
    if (GAgMispredicted != gshareMispredicted) {
        if (GAgMispredicted) {
            if (hybrid2_GAg_gshare[hybrid2Index] < 3) {
                hybrid2_GAg_gshare[hybrid2Index]++;
            }
        } else {
            if (hybrid2_GAg_gshare[hybrid2Index] > 0) {
                hybrid2_GAg_gshare[hybrid2Index]--;
            }
        }
    }
    if (gshareMispredicted != SAgMispredicted) {
        if (gshareMispredicted) {
            if (hybrid2_gshare_SAg[hybrid2Index] < 3) {
                hybrid2_gshare_SAg[hybrid2Index]++;
            }
        } else {
            if (hybrid2_gshare_SAg[hybrid2Index] > 0) {
                hybrid2_gshare_SAg[hybrid2Index]--;
            }
        }
    }
}

VOID UpdateBTBPrediction(ADDRINT instructionAddress, UINT32 instructionSize, ADDRINT branchTarget, BOOL taken) {
    directionData[2]++;
    
    // BTB1
    ADDRINT BTB1Index = instructionAddress & 0x7F;
    ADDRINT BTB1Tag = instructionAddress >> 7;
    INT32 BTB1Way = -1;

    for (INT32 i = 0; i < 4; i++) {
        if (BTB1[BTB1Index][i].valid && BTB1[BTB1Index][i].tag == BTB1Tag) {
            BTB1Way = i;
            break;
        }
    }

    if (BTB1Way == -1) {
        // Miss
        btbPredictorData[0][1]++;
        btbPredictorData[0][0]++;
        for (INT32 i = 0; i < 4; i++) {
            if (!BTB1[BTB1Index][i].valid) {
                BTB1Way = i;
                break;
            }
        }
        if (BTB1Way == -1) {
            BTB1Way = 0;
            for (INT32 i = 1; i < 4; i++) {
                if (BTB1[BTB1Index][i].LRU < BTB1[BTB1Index][BTB1Way].LRU) {
                    BTB1Way = i;
                }
            }
        }
        BTB1[BTB1Index][BTB1Way].valid = true;
        BTB1[BTB1Index][BTB1Way].tag = BTB1Tag;
        BTB1[BTB1Index][BTB1Way].target = branchTarget;
        BTB1[BTB1Index][BTB1Way].LRU = LRUClock++;
    } else {
        // Hit
        if (BTB1[BTB1Index][BTB1Way].target != branchTarget) {
            btbPredictorData[0][0]++; // mispredict
            BTB1[BTB1Index][BTB1Way].target = branchTarget;
        } else if (!taken) {
            btbPredictorData[0][0]++; // mispredict
        }
        BTB1[BTB1Index][BTB1Way].LRU = LRUClock++;
    }

    // BTB2
    ADDRINT BTB2Index = (instructionAddress & 0x7F) ^ (globalHistory & 0x7F);
    ADDRINT BTB2Tag = instructionAddress;
    INT32 BTB2Way = -1;

    for (INT32 i = 0; i < 4; i++) {
        if (BTB2[BTB2Index][i].valid && BTB2[BTB2Index][i].tag == BTB2Tag) {
            BTB2Way = i;
            break;
        }
    }

    if (BTB2Way == -1) {
        // Miss
        btbPredictorData[1][1]++;
        btbPredictorData[1][0]++;
        for (INT32 i = 0; i < 4; i++) {
            if (!BTB2[BTB2Index][i].valid) {
                BTB2Way = i;
                break;
            }
        }
        if (BTB2Way == -1) {
            BTB2Way = 0;
            for (INT32 i = 1; i < 4; i++) {
                if (BTB2[BTB2Index][i].LRU < BTB2[BTB2Index][BTB2Way].LRU) {
                    BTB2Way = i;
                }
            }
        }
        BTB2[BTB2Index][BTB2Way].valid = true;
        BTB2[BTB2Index][BTB2Way].tag = BTB2Tag;
        BTB2[BTB2Index][BTB2Way].target = branchTarget;
        BTB2[BTB2Index][BTB2Way].LRU = LRUClock++;
    } else {
        // Hit
        if (BTB2[BTB2Index][BTB2Way].target != branchTarget) {
            btbPredictorData[1][0]++; // mispredict
            BTB2[BTB2Index][BTB2Way].target = branchTarget;
        } else if (!taken) {
            btbPredictorData[1][0]++; // mispredict
        }
        BTB2[BTB2Index][BTB2Way].LRU = LRUClock++;
    }
}
/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage() {
    cerr << "This tool prints out the number of dynamically executed " << endl
         << "instructions, basic blocks and threads in the application." << endl
         << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

VOID InstrumentConditionalBranch(INS ins) {
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) IsFastForwardDone, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateDirectionPredictors, IARG_INST_PTR, IARG_BRANCH_TARGET_ADDR, IARG_BRANCH_TAKEN, IARG_END);
}

VOID InstrumentIndirectControlTransfer(INS ins) {
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) IsFastForwardDone, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateBTBPrediction, IARG_INST_PTR, IARG_UINT32, INS_Size(ins), IARG_BRANCH_TARGET_ADDR, IARG_BRANCH_TAKEN, IARG_END);
}

VOID Terminate() {
    Fini(0, 0);
    exit(0);
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */
VOID Trace(TRACE trace, VOID* v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        BBL_InsertIfCall(bbl, IPOINT_BEFORE, (AFUNPTR) CheckTerminate, IARG_END);
        BBL_InsertThenCall(bbl, IPOINT_BEFORE, (AFUNPTR) Terminate, IARG_END);

        BBL_InsertIfCall(bbl, IPOINT_BEFORE, (AFUNPTR) CheckFastForward, IARG_END);
        BBL_InsertThenCall(bbl, IPOINT_BEFORE, (AFUNPTR) FastForwardDone, IARG_END);

        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            if (INS_IsBranch(ins) && INS_HasFallThrough(ins)) {
                InstrumentConditionalBranch(ins);
            } else if (INS_IsIndirectControlFlow(ins)) {
                InstrumentIndirectControlTransfer(ins);
            }
        }

        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) CountInstruction, IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v) {
    *out << "Total instructions: " << instructionCount << endl;
    *out << "===============================================" << endl;
    *out << "Direction Predictors" << endl;
    for (UINT32 i = 0; i < 8; i++) {
        *out << directionPredictorNames[i] << ": ";
        *out << "Accesses " << directionData[0] + directionData[1] << ", ";
        *out << "Mispredictions " << directionPredictorData[i][0] + directionPredictorData[i][1] << " (" << ((directionPredictorData[i][0] + directionPredictorData[i][1]) * 100.0f / ((directionData[0] + directionData[1]) * 1.0f)) << "), ";
        *out << "Forward Branches " << directionData[0] << ", ";
        *out << "Forward Mispredictions " << directionPredictorData[i][0] << " (" << ((directionPredictorData[i][0] * 100.0f / directionData[0] * 1.0f)) << "), ";
        *out << "Backward Branches " << directionData[1] << ", ";
        *out << "Backward Mispredictions " << directionPredictorData[i][1] << " (" << ((directionPredictorData[i][1] * 100.0f / directionData[1] * 1.0f)) << ")" << endl;
    }
    *out << endl;

    *out << "BTB Predictors" << endl;
    *out << "BTB1: ";
    *out << "Accesses " << directionData[2] << ", Mispredictions " << btbPredictorData[0][0] << " (" << (btbPredictorData[0][0] * 100.0f / directionData[2]) << "), Misses " << btbPredictorData[0][1] << " (" << (btbPredictorData[0][1] * 100.0f / directionData[2]) << ")" << endl;
    *out << "BTB2: ";
    *out << "Accesses " << directionData[2] << ", Mispredictions " << btbPredictorData[1][0] << " (" << (btbPredictorData[1][0] * 100.0f / directionData[2]) << "), Misses " << btbPredictorData[1][1] << " (" << (btbPredictorData[1][1] * 100.0f / directionData[2]) << ")" << endl;
    *out << "===============================================" << endl;
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[]) {
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv)) {
        return Usage();
    }

    string fileName = KnobOutputFile.Value();
    fastForward = KnobFastForward.Value() * 1e9;

    if (!fileName.empty()) {
        out = new std::ofstream(fileName.c_str());
    }

    if (KnobCount) {
        // Register function to be called to instrument traces
        TRACE_AddInstrumentFunction(Trace, 0);

        // Register function to be called when the application exits
        PIN_AddFiniFunction(Fini, 0);
    }

    cerr << "===============================================" << endl;
    cerr << "This application is instrumented by MyPinTool" << endl;
    if (!KnobOutputFile.Value().empty()) {
        cerr << "See file " << KnobOutputFile.Value() << " for analysis results" << endl;
    }
    cerr << "===============================================" << endl;

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
