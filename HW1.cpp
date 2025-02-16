/*
 * Copyright (C) 2007-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include "types_vmapi.PH"
#include <iostream>
#include <fstream>
#include <types.h>
#include <unordered_map>
using std::cerr;
using std::endl;
using std::string;
using std::unordered_map;

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

unordered_map<UINT64, bool> instructionBlocks;
unordered_map<UINT64, bool> memoryBlocks;

std::ostream *out = &cerr;

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
VOID InsBlockAccess(UINT64 block) { instructionBlocks[block] = true; }
VOID MemBlockAccess(IMULTI_ELEMENT_OPERAND* memOpInfo) {
    for (UINT32 i = 0; i < memOpInfo->NumOfElements(); i++) {
        if (!memOpInfo->IsMemory()) continue;

        UINT64 addr = memOpInfo->ElementAddress(i);
        UINT64 size = memOpInfo->ElementSize(i);
        UINT64 currBlock = addr >> 5;
        do {
            memoryBlocks[currBlock] = true;
            currBlock++;
        } while (currBlock < ((addr + size) >> 5));
    }
}

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
            UINT32 size = (INS_MemoryOperandSize(ins, memOp) + 3) / 4; // number of 32 byte accesses
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END); // fast forwarding
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) CountLoad, IARG_UINT32, size, IARG_END);
        }
        
        if (INS_MemoryOperandIsWritten(ins, memOp)) {
            UINT32 size = (INS_MemoryOperandSize(ins, memOp) + 3) / 4; // number of 32 byte accesses
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

    for (UINT32 op = 0; op < INS_OperandCount(ins); op++) {
        if (INS_OperandIsMemory(ins,op) && INS_OperandElementCount(ins, op) > 1) {
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) FastForward, IARG_END);
            INS_InsertThenPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) MemBlockAccess, IARG_MULTI_ELEMENT_OPERAND, op, IARG_END);
        }
    }
}

VOID InstructionCheckTermination(INS ins, VOID *v) {
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR) Terminate, IARG_END); // check termination
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR) ExitRoutine, IARG_END);
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
        INS_AddInstrumentFunction(InstructionCheckTermination, 0);
        INS_AddInstrumentFunction(InstructionTypeCounter, 0);
        INS_AddInstrumentFunction(InstructionMemoryFootprint, 0);

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
