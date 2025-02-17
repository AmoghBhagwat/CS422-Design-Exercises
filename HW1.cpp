#include "pin.H"
#include "types_core.PH"
#include "types_vmapi.PH"
#include <iostream>
#include <fstream>
#include <types.h>
#include <unordered_set>
using std::cerr;
using std::endl;
using std::string;
using std::unordered_set;

/* ================================================================== */
// Global variables
/* ================================================================== */
VOID Fini(INT32 code, VOID* v);

std::ostream* out = &cerr;
UINT64 fastForward = 0;

enum InstructionCategory : UINT64 {
    LOAD,
    STORE,
    NOP,
    DIRECT_CALL,
    INDIRECT_CALL,
    RETURN,
    UNCONDITIONAL_BRANCH,
    CONDITIONAL_BRANCH,
    LOGICAL,
    ROTATE_SHIFT,
    FLAG,
    VECTOR,
    CONDITIONAL_MOVE,
    MMX_SSE,
    SYSTEM_CALL,
    FLOATING_POINT,
    OTHER
};

UINT64 instructionCount = 0;
UINT64 instructionMetrics[OTHER + 1] = {0};

UINT64 instructionLengthResults[20];
UINT64 memOperandCountResults[5];
UINT64 memReadCountResults[5];
UINT64 memWriteCountResults[5];
UINT64 operandCountResults[10];
UINT64 regReadCountResults[10];
UINT64 regWriteCountResults[10];

UINT64 maxMemBytes = 0;
UINT64 totalMemBytes = 0;

INT32 maxImmediate = INT_MIN;
INT32 minImmediate = INT_MAX;

ADDRDELTA maxDisplacement = INT_MIN;
ADDRDELTA minDisplacement = INT_MAX;

unordered_set<UINT64> memoryBlocks;
unordered_set<UINT64> instructionBlocks;

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

VOID CountInstruction(UINT32 count) { instructionCount += count; }

VOID MemoryBlockAnalysis(ADDRINT address, UINT32 size) {
    maxMemBytes = (size > maxMemBytes) ? size : maxMemBytes;
    totalMemBytes += size;
    for (UINT64 block = (address >> 5); block < ((address + size + 31) >> 5); block++) {
        memoryBlocks.insert(block);
    }
}

VOID PredicatedInstructionAnalysis(
    InstructionCategory category,
    UINT64 loadSize,
    UINT64 storeSize,
    UINT64 memReadCount,
    UINT64 memWriteCount,
    ADDRDELTA minDisp,
    ADDRDELTA maxDisp,
    UINT32 memOpCount,
    UINT32 isMemOp
) {
    instructionMetrics[category]++;
    instructionMetrics[LOAD] += loadSize;
    instructionMetrics[STORE] += storeSize;
    memOperandCountResults[memReadCount + memWriteCount] += isMemOp;
    memReadCountResults[memReadCount]++;
    memWriteCountResults[memWriteCount]++;
    minDisplacement = (minDisp < minDisplacement) ? minDisp : minDisplacement;
    maxDisplacement = (maxDisp > maxDisplacement) ? maxDisp : maxDisplacement;
}

VOID InstructionAnalysis(
    UINT64 instructionLength,
    ADDRINT instructionPointer,
    UINT64 operandCount,
    UINT64 regReadCount,
    UINT64 regWriteCount,
    INT32 minImm,
    INT32 maxImm
) {
    instructionLengthResults[instructionLength]++;
    operandCountResults[operandCount]++;
    regReadCountResults[regReadCount]++;
    regWriteCountResults[regWriteCount]++;
    maxImmediate = (maxImm > maxImmediate) ? maxImm : maxImmediate;
    minImmediate = (minImm < minImmediate) ? minImm : minImmediate;

    totalMemBytes += instructionLength;
    for (UINT64 block = (instructionPointer >> 5); block < (((instructionPointer + instructionLength) >> 5) + ((instructionPointer + instructionLength) % 32 != 0)); block++) {
        instructionBlocks.insert(block);
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

VOID InstrumentInstruction(INS ins) {
    InstructionCategory instructionCategory;
    UINT64 loadSize = 0;
    UINT64 storeSize = 0;
    UINT64 memReadCount = 0;
    UINT64 memWriteCount = 0;
    ADDRDELTA minDisp = INT_MAX;
    ADDRDELTA maxDisp = INT_MIN;
    INT32 minImm = INT_MAX;
    INT32 maxImm = INT_MIN;
    UINT32 memOpCount = INS_MemoryOperandCount(ins);

    // Type A categories
    INT32 category = INS_Category(ins);
    if (category == XED_CATEGORY_NOP) {
        instructionCategory = NOP;
    } else if (category == XED_CATEGORY_CALL) {
        if (INS_IsDirectCall(ins)) {
            instructionCategory = DIRECT_CALL;
        } else {
            instructionCategory = INDIRECT_CALL;
        }
    } else if (category == XED_CATEGORY_RET) {
        instructionCategory = RETURN;
    } else if (category == XED_CATEGORY_X87_ALU) {
        instructionCategory = FLOATING_POINT;
    } else {
        if (category == XED_CATEGORY_COND_BR) {
            instructionCategory = CONDITIONAL_BRANCH;
        } else if (category == XED_CATEGORY_UNCOND_BR) {
            instructionCategory = UNCONDITIONAL_BRANCH;
        } else if (category == XED_CATEGORY_LOGICAL) {
            instructionCategory = LOGICAL;
        } else if (category == XED_CATEGORY_ROTATE || category == XED_CATEGORY_SHIFT) {
            instructionCategory = ROTATE_SHIFT;
        } else if (category == XED_CATEGORY_FLAGOP) {
            instructionCategory = FLAG;
        } else if (category == XED_CATEGORY_AVX || category == XED_CATEGORY_AVX2 || category == XED_CATEGORY_AVX2GATHER || category == XED_CATEGORY_AVX512) {
            instructionCategory = VECTOR;
        } else if (category == XED_CATEGORY_CMOV) {
            instructionCategory = CONDITIONAL_MOVE;
        } else if (category == XED_CATEGORY_MMX || category == XED_CATEGORY_SSE) {
            instructionCategory = MMX_SSE;
        } else if (category == XED_CATEGORY_SYSCALL) {
            instructionCategory = SYSTEM_CALL;
        } else {
            instructionCategory = OTHER;
        }
    }

    // Type B Categories
    for (UINT32 memOp = 0; memOp < memOpCount; memOp++) {
        UINT64 size = INS_MemoryOperandSize(ins, memOp);
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            loadSize = (size + 3) / 4;
            memReadCount++;
        }
        if (INS_MemoryOperandIsWritten(ins, memOp)) { 
            storeSize = (size + 3) / 4;
            memWriteCount++;
        }
        ADDRDELTA displacement = INS_OperandMemoryDisplacement(ins, memOp);
        minDisp = (displacement < minDisp) ? displacement : minDisp;
        maxDisp = (displacement > maxDisp) ? displacement : maxDisp;

        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) CheckFastForward, IARG_END);
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) MemoryBlockAnalysis, IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE, IARG_END);
    }

    for (UINT32 i = 0; i < INS_OperandCount(ins); i++) {
        if (INS_OperandIsImmediate(ins, i)) {
            INT32 imm = INS_OperandImmediate(ins, i);
            minImm = (imm < minImm) ? imm : minImm;
            maxImm = (imm > maxImm) ? imm : maxImm;
        }
    }

    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) CheckFastForward, IARG_END);
    INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) PredicatedInstructionAnalysis,
        IARG_UINT64, (UINT64) instructionCategory,
        IARG_UINT64, (UINT64) loadSize,
        IARG_UINT64, (UINT64) storeSize,
        IARG_UINT64, (UINT64) memReadCount,
        IARG_UINT64, (UINT64) memWriteCount,
        IARG_ADDRINT, (ADDRINT) minDisp,
        IARG_ADDRINT, (ADDRINT) maxDisp,
        IARG_UINT32, (UINT32) (INS_IsMemoryRead(ins) || INS_IsMemoryWrite(ins)),
        IARG_END
    );

    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) CheckFastForward, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) InstructionAnalysis,
        IARG_UINT64, (UINT64) INS_Size(ins),
        IARG_ADDRINT, (ADDRINT) INS_Address(ins),
        IARG_UINT64, (UINT64) INS_OperandCount(ins),
        IARG_UINT64, (UINT64) INS_MaxNumRRegs(ins),
        IARG_UINT64, (UINT64) INS_MaxNumWRegs(ins),
        IARG_ADDRINT, (ADDRINT) minImm,
        IARG_ADDRINT, (ADDRINT) maxImm,
        IARG_END
    );
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
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            InstrumentInstruction(ins);
        }
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) CountInstruction, IARG_UINT32, BBL_NumIns(bbl), IARG_END);

        BBL_InsertIfCall(bbl, IPOINT_BEFORE, (AFUNPTR) CheckTerminate, IARG_END);
        BBL_InsertThenCall(bbl, IPOINT_BEFORE, (AFUNPTR) Terminate, IARG_END);
    }
}

double calculateCpi() {
    UINT64 total = 0;
    double cpi = 0;
    for (UINT64 i = 0; i < OTHER + 1; i++) {
        total += instructionMetrics[i];
        cpi += 1.0 * instructionMetrics[i];
    }

    cpi += 69.0 * instructionMetrics[LOAD];
    cpi += 69.0 * instructionMetrics[STORE];

    cpi /= (1.0) * total;
    return cpi;
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v) {
    UINT64 totalInstructions = 0;
    for (UINT64 i = 0; i < OTHER + 1; i++) {
        totalInstructions += instructionMetrics[i];
    }

    *out << "Analysed " << instructionCount << " instructions" << endl;
    *out << "===============================================" << endl;
    *out << "Instruction Type Results:" << endl;
    *out << "Loads: " << instructionMetrics[LOAD] << " (" << 100.0 * instructionMetrics[LOAD] / totalInstructions << "%)" << endl;
    *out << "Stores: " << instructionMetrics[STORE] << " (" << 100.0 * instructionMetrics[STORE] / totalInstructions << "%)" << endl;
    *out << "Nops: " << instructionMetrics[NOP] << " (" << 100.0 * instructionMetrics[NOP] / totalInstructions << "%)" << endl;
    *out << "Direct Calls: " << instructionMetrics[DIRECT_CALL] << " (" << 100.0 * instructionMetrics[DIRECT_CALL] / totalInstructions << "%)" << endl;
    *out << "Indirect Calls: " << instructionMetrics[INDIRECT_CALL] << " (" << 100.0 * instructionMetrics[INDIRECT_CALL] / totalInstructions << "%)" << endl;
    *out << "Returns: " << instructionMetrics[RETURN] << " (" << 100.0 * instructionMetrics[RETURN] / totalInstructions << "%)" << endl;
    *out << "Unconditional Branches: " << instructionMetrics[UNCONDITIONAL_BRANCH] << " (" << 100.0 * instructionMetrics[UNCONDITIONAL_BRANCH] / totalInstructions << "%)" << endl;
    *out << "Conditional Branches: " << instructionMetrics[CONDITIONAL_BRANCH] << " (" << 100.0 * instructionMetrics[CONDITIONAL_BRANCH] / totalInstructions << "%)" << endl;
    *out << "Logical: " << instructionMetrics[LOGICAL] << " (" << 100.0 * instructionMetrics[LOGICAL] / totalInstructions << "%)" << endl;
    *out << "Rotate/Shift: " << instructionMetrics[ROTATE_SHIFT] << " (" << 100.0 * instructionMetrics[ROTATE_SHIFT] / totalInstructions << "%)" << endl;
    *out << "Flag: " << instructionMetrics[FLAG] << " (" << 100.0 * instructionMetrics[FLAG] / totalInstructions << "%)" << endl;
    *out << "Vector: " << instructionMetrics[VECTOR] << " (" << 100.0 * instructionMetrics[VECTOR] / totalInstructions << "%)" << endl;
    *out << "Conditional Move: " << instructionMetrics[CONDITIONAL_MOVE] << " (" << 100.0 * instructionMetrics[CONDITIONAL_MOVE] / totalInstructions << "%)" << endl;
    *out << "MMX/SSE: " << instructionMetrics[MMX_SSE] << " (" << 100.0 * instructionMetrics[MMX_SSE] / totalInstructions << "%)" << endl;
    *out << "System Calls: " << instructionMetrics[SYSTEM_CALL] << " (" << 100.0 * instructionMetrics[SYSTEM_CALL] / totalInstructions << "%)" << endl;
    *out << "Floating Point: " << instructionMetrics[FLOATING_POINT] << " (" << 100.0 * instructionMetrics[FLOATING_POINT] / totalInstructions << "%)" << endl;
    *out << "Other: " << instructionMetrics[OTHER] << " (" << 100.0 * instructionMetrics[OTHER] / totalInstructions << "%)" << endl;
    *out << "CPI: " << calculateCpi() << endl;
    *out << "===============================================" << endl;
    *out << "Instruction Size Results:" << endl;
    for (int i = 0; i < 20; i++) {
        *out << i << " : " << instructionLengthResults[i] << endl;
    }
    *out << "Mem Operand Count Results:" << endl;
    for (int i = 0; i < 5; ++i) {
        *out << i << " : " << memOperandCountResults[i] << endl;
    }

    *out << "Memory Instruction Read Operand Results: " << endl;
    for (int i = 0; i < 5; ++i) {
        *out << i << " : " << memReadCountResults[i] << endl;
    }

    *out << "Memory Instruction Write Operand Results: " << endl;
    for (int i = 0; i < 5; ++i) {
        *out << i << " : " << memWriteCountResults[i] << endl;
    }

    *out << "Instruction Operand Results: " << endl;
    for (int i = 0; i < 10; ++i) {
        *out << i << " : " << operandCountResults[i] << endl;
    }

    *out << "Instruction Register Read Operand Results: " << endl;
    for (int i = 0; i < 10; ++i) {
        *out << i << " : " << regReadCountResults[i] << endl;
    }

    *out << "Instruction Register Write Operand Results: " << endl;
    for (int i = 0; i < 10; ++i) {
        *out << i << " : " << regWriteCountResults[i] << endl;
    }

    UINT64 memInstrCount = 0;
    for(int i = 1; i < 5; ++i) {
        memInstrCount += memOperandCountResults[i];
    }

    double avgMemBytes = (totalMemBytes * 1.0) / memInstrCount;

    *out << "Instruction Blocks Accesses : " << instructionBlocks.size() << endl;
    *out << "Memory Blocks Accesses : " << memoryBlocks.size() << endl;
    *out << "Maximum number of bytes touched by an instruction : " << maxMemBytes << endl;
    *out << "Average number of bytes touched by an instruction : " << avgMemBytes << endl;
    *out << "Maximum value of immediate : " << maxImmediate << endl;
    *out << "Minimum value of immediate : " << minImmediate << endl;
    *out << "Maximum value of displacement used in memory addressing : " << maxDisplacement << endl;
    *out << "Minimum value of displacement used in memory addressing : " << minDisplacement << endl;
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
