#include "pin.H"
#include "types_vmapi.PH"
#include <iostream>
#include <fstream>
#include <types.h>
#include <unordered_set>
#include <limits.h>

using std::cerr;
using std::endl;
using std::string;
using std::unordered_set;

/* ================================================================== */
// Global variables
/* ================================================================== */

UINT64 fastForwardCount; // take from command line
UINT64 insCount = 0;    // number of dynamically executed instructions
UINT64 analysedInsCount = 0;

UINT64 loadCount = 0;
UINT64 storeCount = 0;
UINT64 nopCount = 0;
UINT64 directCallCount = 0;
UINT64 indirectCallCount = 0;
UINT64 returnCount = 0;
UINT64 uncondBranchCount = 0;
UINT64 condBranchCount = 0;
UINT64 logicalOpCount = 0;
UINT64 rotshiftOpCount = 0;
UINT64 flagCount = 0;
UINT64 vectorCount = 0;
UINT64 condMoveCount = 0;
UINT64 mmxSseCount = 0;
UINT64 syscallCount = 0;
UINT64 fpCount = 0;
UINT64 restCount = 0;

unordered_set<ADDRINT> instructionBlocks;
unordered_set<ADDRINT> memoryBlocks;

std::ostream *out = &cerr;

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

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");

KNOB<BOOL> KnobCount(KNOB_MODE_WRITEONCE, "pintool", "count", "1",
                     "count instructions, basic blocks and threads in the application");

KNOB<UINT64> KnobCountFastForward(KNOB_MODE_WRITEONCE, "pintool", "f", "0",
                     "fast forward to this instruction count");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool prints out the number of dynamically executed " << endl
         << "instructions, basic blocks and threads in the application." << endl
         << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

/* ===================================================================== */
// Function declarations
/* ===================================================================== */
VOID Fini(INT32 code, VOID *v);
double calculateCpi();

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

VOID CountIns() {
    insCount++;
}

UINT32 FastForward() {
    return (insCount >= fastForwardCount) && (insCount < fastForwardCount + 1000000000);
}

UINT32 Terminate() {
    return insCount >= fastForwardCount + 1000000000;
}

VOID ExitRoutine() {
    Fini(0,0);
    exit(0);
}

VOID CountLoad(UINT32 size) { loadCount += size; }
VOID CountStore(UINT32 size) { storeCount += size; }
VOID CountNop() { nopCount++; }
VOID CountDirectCall() { directCallCount++; }
VOID CountIndirectCall() { indirectCallCount++; }
VOID CountReturn() { returnCount++; }
VOID CountUncondBranch() { uncondBranchCount++; }
VOID CountCondBranch() { condBranchCount++; }
VOID CountLogicalOp() { logicalOpCount++; }
VOID CountRotShiftOp() { rotshiftOpCount++; }
VOID CountFlag() { flagCount++; }
VOID CountVector() { vectorCount++; }
VOID CountCondMove() { condMoveCount++; }
VOID CountMmxSse() { mmxSseCount++; }
VOID CountSyscall() { syscallCount++; }
VOID CountFloatingPoint() { fpCount++; }
VOID CountRest() { restCount++; }

VOID InsBlockAccess(UINT64 block) { instructionBlocks.insert(block); }
VOID MemBlockAccess(ADDRINT address, UINT32 size) {
    memoryBlocks.insert(address>>5);
    for (UINT64 block = (address>>5); block < ((address + size)>>5); ++block) {
        memoryBlocks.insert(block);
    }
}

VOID CountInstructionLength(UINT32 size) { instructionLengthResults[size]++; }
VOID CountOperandCount(UINT32 count) { operandCountResults[count]++; }
VOID CountRegReadCount(UINT32 count) { regReadCountResults[count]++; }
VOID CountRegWriteCount(UINT32 count) { regWriteCountResults[count]++; }
VOID CountMemOperandCount(UINT32 count) { memOperandCountResults[count]++; }
VOID CountMemReadCount(UINT32 count) { memReadCountResults[count]++; }
VOID CountMemWriteCount(UINT32 count) { memWriteCountResults[count]++; }
VOID CountMemBytes(UINT32 bytes) { totalMemBytes += bytes; }

UINT32 CheckMaxMemBytes(UINT32 bytes) { return bytes > maxMemBytes; }
VOID UpdateMaxMemBytes(UINT32 bytes) { maxMemBytes = bytes; }
UINT32 CheckMinImmediate(INT32 imm) { return imm < minImmediate; }
VOID UpdateMinImmediate(INT32 imm) { minImmediate = imm; }
UINT32 CheckMaxImmediate(INT32 imm) { return imm > maxImmediate; }
VOID UpdateMaxImmediate(INT32 imm) { maxImmediate = imm; }
UINT32 CheckMinDisplacement(ADDRDELTA disp) { return disp < minDisplacement; }
VOID UpdateMinDisplacement(ADDRDELTA disp) { minDisplacement = disp; }
UINT32 CheckMaxDisplacement(ADDRDELTA disp) { return disp > maxDisplacement; }
VOID UpdateMaxDisplacement(ADDRDELTA disp) { maxDisplacement = disp; }

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */
VOID InstructionTypeCounter(INS ins, VOID *v) {
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END); // fast forwarding
    // Type A categories
    INT32 category = INS_Category(ins);
    if (category == XED_CATEGORY_NOP) {
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountNop, IARG_END);
    } else if (category == XED_CATEGORY_CALL) {
        if (INS_IsDirectCall(ins)) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountDirectCall, IARG_END);
        } else {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountIndirectCall, IARG_END);
        }
    } else if (category == XED_CATEGORY_RET) {
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountReturn, IARG_END);
    } else if (category == XED_CATEGORY_X87_ALU) {
        INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountFloatingPoint, IARG_END);
    } else {
        if (category == XED_CATEGORY_COND_BR) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountCondBranch, IARG_END);
        } else if (category == XED_CATEGORY_UNCOND_BR) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountUncondBranch, IARG_END);
        } else if (category == XED_CATEGORY_LOGICAL) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountLogicalOp, IARG_END);
        } else if (category == XED_CATEGORY_ROTATE || category == XED_CATEGORY_SHIFT) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountRotShiftOp, IARG_END);
        } else if (category == XED_CATEGORY_FLAGOP) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountFlag, IARG_END);
        } else if (category == XED_CATEGORY_AVX || category == XED_CATEGORY_AVX2 || category == XED_CATEGORY_AVX2GATHER || category == XED_CATEGORY_AVX512) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountVector, IARG_END);
        } else if (category == XED_CATEGORY_CMOV) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountCondMove, IARG_END);
        } else if (category == XED_CATEGORY_MMX || category == XED_CATEGORY_SSE) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountMmxSse, IARG_END);
        } else if (category == XED_CATEGORY_SYSCALL) {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountSyscall, IARG_END);
        } else {
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountRest, IARG_END);
        }
    }

    // Type B Categories
    UINT32 memOperands = INS_MemoryOperandCount(ins);
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            UINT32 size = (INS_MemoryOperandSize(ins, memOp) + 3) / 4; // number of 32 bit accesses
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END); // fast forwarding
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountLoad, IARG_UINT32, size, IARG_END);
        }
        
        if (INS_MemoryOperandIsWritten(ins, memOp)) {
            UINT32 size = (INS_MemoryOperandSize(ins, memOp) + 3) / 4; // number of 32 bit accesses
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END); // fast forwarding
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountStore, IARG_UINT32, size, IARG_END);
        }
    }

    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) CountIns, IARG_END);
}

VOID InstructionMemoryFootprint(INS ins, VOID *v) {
    ADDRINT instructionPointer = INS_Address(ins);
    UINT64 instructionSize = INS_Size(ins);

    UINT64 block = instructionPointer >> 5;
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) InsBlockAccess, IARG_UINT64, block, IARG_END);

    UINT64 nextblock = (instructionPointer + instructionSize) >> 5;
    if (nextblock != block) {
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) InsBlockAccess, IARG_UINT64, nextblock, IARG_END);
    }

    UINT32 memOperands = INS_MemoryOperandCount(ins);
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp) || INS_MemoryOperandIsWritten(ins, memOp)) {
            UINT32 size = INS_MemoryOperandSize(ins, memOp);
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) MemBlockAccess, IARG_MEMORYOP_EA, memOp, IARG_UINT32, size, IARG_END);
        }
    }
}

VOID InstructionCheckTermination(INS ins, VOID *v) {
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) Terminate, IARG_END); // check termination
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) ExitRoutine, IARG_END);
}

VOID InstructionLength(INS ins, VOID *v) {
    UINT32 size = INS_Size(ins);
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) CountInstructionLength, IARG_UINT32, size, IARG_END);
}

VOID OperandCount(INS ins, VOID *v) {
    UINT32 count = INS_OperandCount(ins);
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) CountOperandCount, IARG_UINT32, count, IARG_END);
}

VOID RegReadCount(INS ins, VOID *v) {
    UINT32 count = INS_MaxNumRRegs(ins);
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) CountRegReadCount, IARG_UINT32, count, IARG_END);
}

VOID RegWriteCount(INS ins, VOID *v) {
    UINT32 count = INS_MaxNumWRegs(ins);
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) CountRegWriteCount, IARG_UINT32, count, IARG_END);
}

VOID MemOperandCount(INS ins, VOID *v) {
    UINT32 count = INS_MemoryOperandCount(ins);
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountMemOperandCount, IARG_UINT32, count, IARG_END);
}

VOID MemReadCount(INS ins, VOID *v) {
    UINT32 count = 0;
    for (UINT32 i = 0; i < INS_MemoryOperandCount(ins); i++) {
        if (INS_MemoryOperandIsRead(ins, i)) {
            count++;
        }
    }
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountMemReadCount, IARG_UINT32, count, IARG_END);
}

VOID MemWriteCount(INS ins, VOID *v) {
    UINT32 count = 0;
    for (UINT32 i = 0; i < INS_MemoryOperandCount(ins); i++) {
        if (INS_MemoryOperandIsWritten(ins, i)) {
            count++;
        }
    }
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountMemWriteCount, IARG_UINT32, count, IARG_END);
}

VOID MemBytes(INS ins, VOID *v) {
    UINT32 bytes = 0;
    for (UINT32 i = 0; i < INS_MemoryOperandCount(ins); i++) {
        bytes += INS_MemoryOperandSize(ins, i);
    }
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
    INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountMemBytes, IARG_UINT32, bytes, IARG_END);

    if (bytes > maxMemBytes) {
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateMaxMemBytes, IARG_UINT32, bytes, IARG_END);
    }
}

VOID ImmediateValue(INS ins, VOID *v) {
    for (UINT32 i = 0; i < INS_OperandCount(ins); i++) {
        if (INS_OperandIsImmediate(ins, i)) {
            INT32 immediate = INS_OperandImmediate(ins, i);
            if (immediate < minImmediate) {
                INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
                INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateMinImmediate, IARG_ADDRINT, immediate, IARG_END);
            } else if (immediate > maxImmediate) {
                INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
                INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateMaxImmediate, IARG_ADDRINT, immediate, IARG_END);
            }
        }
    }
}

VOID DisplacementValue(INS ins, VOID *v) {
    for (UINT32 i = 0; i < INS_OperandCount(ins); i++) {
        if (INS_OperandIsMemory(ins, i)) {
            ADDRDELTA displacement = INS_OperandMemoryDisplacement(ins, i);
            if (displacement < minDisplacement) {
                INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
                INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateMinDisplacement, IARG_ADDRINT, displacement, IARG_END);
            } else if (displacement > maxDisplacement) {
                INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
                INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) UpdateMaxDisplacement, IARG_ADDRINT, displacement, IARG_END);
            }
        }
    }
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{
    UINT64 total = loadCount + storeCount + nopCount + directCallCount + indirectCallCount + returnCount + uncondBranchCount + condBranchCount + logicalOpCount + rotshiftOpCount + flagCount + vectorCount + condMoveCount + mmxSseCount + syscallCount + fpCount + restCount;
    double cpi = calculateCpi();
    
    *out << "Instruction count = " << insCount << endl;
    *out << "===============================================" << endl;
    *out << "Instruction Type Results" << endl;
    *out << "Loads: " << loadCount << " (" << (loadCount * 1.0 / total) << ")" << endl;
    *out << "Stores: " << storeCount << " (" << (storeCount * 1.0 / total) << ")" << endl;
    *out << "NOPs: " << nopCount << " (" << (nopCount * 1.0 / total) << ")" << endl;
    *out << "Direct Calls: " << directCallCount << " (" << (directCallCount * 1.0 / total) << ")" << endl;
    *out << "Indirect Calls: " << indirectCallCount << " (" << (indirectCallCount * 1.0 / total) << ")" << endl;
    *out << "Returns: " << returnCount << " (" << (returnCount * 1.0 / total) << ")" << endl;
    *out << "Unconditional Branches: " << uncondBranchCount << " (" << (uncondBranchCount * 1.0 / total) << ")" << endl;
    *out << "Conditional Branches: " << condBranchCount << " (" << (condBranchCount * 1.0 / total) << ")" << endl;
    *out << "Logical Operations: " << logicalOpCount << " (" << (logicalOpCount * 1.0 / total) << ")" << endl;
    *out << "Rotate/Shift Operations: " << rotshiftOpCount << " (" << (rotshiftOpCount * 1.0 / total) << ")" << endl;
    *out << "Flag Operations: " << flagCount << " (" << (flagCount * 1.0 / total) << ")" << endl;
    *out << "Vector Operations: " << vectorCount << " (" << (vectorCount * 1.0 / total) << ")" << endl;
    *out << "Conditional Moves: " << condMoveCount << " (" << (condMoveCount * 1.0 / total) << ")" << endl;
    *out << "MMX/SSE Operations: " << mmxSseCount << " (" << (mmxSseCount * 1.0 / total) << ")" << endl;
    *out << "Syscalls: " << syscallCount << " (" << (syscallCount * 1.0 / total) << ")" << endl;
    *out << "Floating Point Operations: " << fpCount << " (" << (fpCount * 1.0 / total) << ")" << endl;
    *out << "The rest: " << restCount << " (" << (restCount * 1.0 / total) << ")" << endl;
    *out << "===============================================" << endl;
    *out << "CPI: " << cpi << endl;

    *out << "===============================================" << endl;
    *out << "Instruction block accesses: " << (UINT64) (instructionBlocks.size()) << endl;
    *out << "Memory block accesses: " << (UINT64) (memoryBlocks.size()) << endl;
    *out << "===============================================" << endl;
    *out << "Instruction Size Results: " << endl;
    for (int i = 0; i < 20; ++i) {
        *out << i << " : " << instructionLengthResults[i] << endl;
    }

    *out << "Memory Instruction Operand Results: " << endl;
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


double calculateCpi() {
    double cpi = 0;
    cpi += 70.0 * loadCount;
    cpi += 70.0 * storeCount;
    cpi += 1.0 * nopCount;
    cpi += 1.0 * directCallCount;
    cpi += 1.0 * indirectCallCount;
    cpi += 1.0 * returnCount;
    cpi += 1.0 * uncondBranchCount;
    cpi += 1.0 * condBranchCount;
    cpi += 1.0 * logicalOpCount;
    cpi += 1.0 * rotshiftOpCount;
    cpi += 1.0 * flagCount;
    cpi += 1.0 * vectorCount;
    cpi += 1.0 * condMoveCount;
    cpi += 1.0 * mmxSseCount;
    cpi += 1.0 * syscallCount;
    cpi += 1.0 * fpCount;
    cpi += 1.0 * restCount;

    UINT64 total = loadCount + storeCount + nopCount + directCallCount + indirectCallCount + returnCount + uncondBranchCount + condBranchCount + logicalOpCount + rotshiftOpCount + flagCount + vectorCount + condMoveCount + mmxSseCount + syscallCount + fpCount + restCount;
    cpi /= (1.0) * total;
    return cpi;
}

VOID Trace(TRACE trace, VOID *v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            if (!INS_Valid(ins)) continue;

            InstructionCheckTermination(ins, 0);
            InstructionTypeCounter(ins, 0);
            InstructionMemoryFootprint(ins, 0);
            InstructionLength(ins, 0);
            OperandCount(ins, 0);
            RegReadCount(ins, 0);
            RegWriteCount(ins, 0);
            MemOperandCount(ins, 0);
            MemReadCount(ins, 0);
            MemWriteCount(ins, 0);
            MemBytes(ins, 0);
            ImmediateValue(ins, 0);
            DisplacementValue(ins, 0);
        }
    }
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments,
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    string fileName = KnobOutputFile.Value();

    if (!fileName.empty())
    {
        out = new std::ofstream(fileName.c_str());
    }

    fastForwardCount = KnobCountFastForward.Value() * 1'000'000'000;

    if (KnobCount)
    {
        //INS_AddInstrumentFunction(InstructionCheckTermination, 0);
        //INS_AddInstrumentFunction(InstructionTypeCounter, 0);
        //INS_AddInstrumentFunction(InstructionMemoryFootprint, 0);
        //INS_AddInstrumentFunction(InstructionLength, 0);
        //INS_AddInstrumentFunction(OperandCount, 0);
        //INS_AddInstrumentFunction(RegReadCount, 0);
        //INS_AddInstrumentFunction(RegWriteCount, 0);
        //INS_AddInstrumentFunction(MemOperandCount, 0);
        //INS_AddInstrumentFunction(MemReadCount, 0);
        //INS_AddInstrumentFunction(MemWriteCount, 0);
        //INS_AddInstrumentFunction(MemBytes, 0);
        //INS_AddInstrumentFunction(ImmediateValue, 0);
        //INS_AddInstrumentFunction(DisplacementValue, 0);
        TRACE_AddInstrumentFunction(Trace, 0);

        // Register function to be called when the application exits
        PIN_AddFiniFunction(Fini, 0);
    }

    cerr << "===============================================" << endl;
    cerr << "This application is instrumented by Pin" << endl;
    if (!KnobOutputFile.Value().empty())
    {
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
