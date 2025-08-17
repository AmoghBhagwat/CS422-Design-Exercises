#include "memory.h"

Memory::Memory (Mipc *mc)
{
   _mc = mc;
}

Memory::~Memory (void) {}

void
Memory::MainLoop (void)
{
   while (1) {
      AWAIT_P_PHI0;
      MEM_WB_Register temp;
      temp._ins = _mc->_ex_mem->_ins;
      temp._pc = _mc->_ex_mem->_pc;
      temp._decodedSRC1 = _mc->_ex_mem->_decodedSRC1;
      temp._decodedSRC2 = _mc->_ex_mem->_decodedSRC2;
      temp._decodedDST = _mc->_ex_mem->_decodedDST;
      temp._subregOperand = _mc->_ex_mem->_subregOperand;
      temp._memory_addr_reg = _mc->_ex_mem->_memory_addr_reg;
      temp._opResultHi = _mc->_ex_mem->_opResultHi;
      temp._opResultLo = _mc->_ex_mem->_opResultLo;
      temp._memControl = _mc->_ex_mem->_memControl;
      temp._writeREG = _mc->_ex_mem->_writeREG;
      temp._writeFREG = _mc->_ex_mem->_writeFREG;
      temp._branchOffset = _mc->_ex_mem->_branchOffset;
      temp._hiWPort = _mc->_ex_mem->_hiWPort;
      temp._loWPort = _mc->_ex_mem->_loWPort;
      temp._decodedShiftAmt = _mc->_ex_mem->_decodedShiftAmt;
      temp._isSyscall = _mc->_ex_mem->_isSyscall;
      temp._isIllegalOp = _mc->_ex_mem->_isIllegalOp;
      temp._bdslot = _mc->_ex_mem->_bdslot;
      temp._btgt = _mc->_ex_mem->_btgt;
      temp._memOp = _mc->_ex_mem->_memOp;
      temp._decodedSRC3 = _mc->_ex_mem->_decodedSRC3;
      temp._lo = _mc->_ex_mem->_lo;
      temp._hi = _mc->_ex_mem->_hi;

      for (int i = 0; i < 34; i++) temp._gprForward[i] = _mc->_mem_wb->_gprForward[i];

      if (_mc->_ex_mem->_carryForward != 0) {
         temp._gprForward[_mc->_ex_mem->_carryForward] = _mc->_ex_mem->_gprForward[_mc->_ex_mem->_carryForward];
      }

      if (_mc->_ex_mem->_forwardSrc3 == 2) {
         if (_mc->_ex_mem->_src3 < 32) temp._decodedSRC3 = _mc->_mem_wb->_gprForward[_mc->_ex_mem->_src3];
         else if (_mc->_ex_mem->_src3 == HI) temp._hi = _mc->_mem_wb->_gprForward[HI];
         else if (_mc->_ex_mem->_src3 == LO) temp._lo = _mc->_mem_wb->_gprForward[LO];
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> Use forwarded value of %#x for register %d from MEM-WB\n", SIM_TIME, temp._decodedSRC3, _mc->_ex_mem->_src3);
#endif
      }

      AWAIT_P_PHI1;
      *(_mc->_mem_wb) = temp;
      if (_mc->_mem_wb->_memControl) {
         _mc->_mem_wb->_memOp(_mc, _mc->_mem_wb);
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> Memory involved in ins %#x, using address %#x\n", SIM_TIME, _mc->_mem_wb->_ins, _mc->_mem_wb->_memory_addr_reg);
#endif
         if (_mc->_mem_wb->_writeREG) {
            _mc->_mem_wb->_gprForward[_mc->_mem_wb->_decodedDST] = _mc->_mem_wb->_opResultLo;
#ifdef MIPC_DEBUG
            fprintf(_mc->_debugLog, "<%llu> Write to MEM-WB register %d value %#x\n", SIM_TIME, _mc->_mem_wb->_decodedDST, _mc->_mem_wb->_opResultLo);
#endif
         }
      } else {
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> No memory involved in ins %#x, pc = %#x\n", SIM_TIME, _mc->_ex_mem->_ins, _mc->_ex_mem->_pc);
#endif         
      }
   }
}
