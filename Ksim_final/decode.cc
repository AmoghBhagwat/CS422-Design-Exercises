#include "decode.h"

Decode::Decode (Mipc *mc)
{
   _mc = mc;
}

Decode::~Decode (void) {}

void
Decode::MainLoop (void)
{
   unsigned int ins;
   unsigned int pc;
   while (1) {
      AWAIT_P_PHI0;
      if (_mc->_isInterlock) {
         _mc->_pc = _mc->_if_id->_pc;
         _mc->_if_id->_ins = _mc->_if_id->_prevIns;
         _mc->_if_id->_pc = _mc->_if_id->_prevPc;
         _mc->_nfetched--;
      }

      for (int i = 0; i < 34; i++) {
         if (_mc->_gprReadyCycles[i] > 0) {
            _mc->_gprReadyCycles[i]--;
         }
         if (_mc->_gprForwardedReadyCycles[i] > 0) {
            _mc->_gprForwardedReadyCycles[i]--;
         }
      }
      for (int i = 0; i < 16; i++) {
         if (_mc->_fprReadyCycles[i] > 0) {
            _mc->_fprReadyCycles[i]--;
         }
         if (_mc->_fprForwardedReadyCycles[i] > 0) {
            _mc->_fprForwardedReadyCycles[i]--;
         }
      }
      
      ins = _mc->_if_id->_ins;
      pc = _mc->_if_id->_pc;

      Bool stall = _mc->_isStall;
      
      AWAIT_P_PHI1;
      if (!stall) {
         delete _mc->_id_ex;
         _mc->_id_ex = new ID_EX_Register();
         _mc->_id_ex->_ins = ins;
         _mc->_id_ex->_pc = pc;
         _mc->_isInterlock = FALSE;
         
         unsigned int ready_cycles = _mc->_id_ex->Dec(_mc, _mc->_ex_mem, _mc->_mem_wb, _mc->_id_ex->_ins);
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> ID Received instruction %#x, PC %#x src1 = %d src2 = %d dst = %d src3 = %d\n", SIM_TIME, ins, pc, _mc->_id_ex->_src1, _mc->_id_ex->_src2, _mc->_id_ex->_decodedDST, _mc->_id_ex->_src3);
#endif         

         if (_mc->_id_ex->_isSyscall) {
            _mc->_isStall = TRUE;
            _mc->_isSyscall = TRUE;
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> Decoded instruction %#x to be SYSCALL\n", SIM_TIME, _mc->_id_ex->_ins);
#endif         
         } else if ((_mc->_id_ex->_src1 != 0 && _mc->_gprReadyCycles[_mc->_id_ex->_src1] > 0) ||
                     (_mc->_id_ex->_src2 != 0 && _mc->_gprReadyCycles[_mc->_id_ex->_src2] > 0) ||
                     (_mc->_id_ex->_src3 != 0 && _mc->_gprReadyCycles[_mc->_id_ex->_src3] > 0)) {
            int valid = 1;
            if (_mc->_id_ex->_src1 != 0 && _mc->_gprReadyCycles[_mc->_id_ex->_src1] > 0) {
               if (_mc->_gprForwardedReadyCycles[_mc->_id_ex->_src1] == 0) { // take from EX-MEM register
                  _mc->_id_ex->_forwardSrc1 = 1;
               } else if (_mc->_gprForwardedReadyCycles[_mc->_id_ex->_src1] == 1) { // take from MEM-MEM register
                  _mc->_id_ex->_forwardSrc1 = 2;
               } else {
#ifdef MIPC_DEBUG
                  fprintf(_mc->_debugLog, "<%llu> stall because forward %d not available\n", SIM_TIME, _mc->_id_ex->_src1);
#endif
                  valid = 0;
               }
            }
            if (_mc->_id_ex->_src2 != 0 && _mc->_gprReadyCycles[_mc->_id_ex->_src2] > 0) {
               if (_mc->_gprForwardedReadyCycles[_mc->_id_ex->_src2] == 0) { // take from EX-MEM register
                  _mc->_id_ex->_forwardSrc2 = 1;
               } else if (_mc->_gprForwardedReadyCycles[_mc->_id_ex->_src2] == 1) { // take from MEM-MEM register
                  _mc->_id_ex->_forwardSrc2 = 2;
               } else {
#ifdef MIPC_DEBUG
                  fprintf(_mc->_debugLog, "<%llu> stall because forward %d not available\n", SIM_TIME, _mc->_id_ex->_src2);
#endif
                  valid = 0;
               }
            }
            if (_mc->_id_ex->_src3 != 0 && _mc->_gprReadyCycles[_mc->_id_ex->_src3] > 0) {
               if (_mc->_gprForwardedReadyCycles[_mc->_id_ex->_src3] <= 1) { // take from MEM-WB register
                  _mc->_id_ex->_forwardSrc3 = 2;
               } else {
#ifdef MIPC_DEBUG
                  fprintf(_mc->_debugLog, "<%llu> stall because forward %d not available\n", SIM_TIME, _mc->_id_ex->_src3);
#endif
                  valid = 0;
               }
            }
            if (valid) {
#ifdef MIPC_DEBUG
               fprintf(_mc->_debugLog, "<%llu> Using forwarded values: src1 (%d) and src2 (%d) src3 (%d)\n", SIM_TIME, _mc->_id_ex->_forwardSrc1, _mc->_id_ex->_forwardSrc2, _mc->_id_ex->_forwardSrc3);
#endif               
               goto legal_op;   
            }
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> Instruction %#x operands not ready, adding interlock src1 = %d src2 = %d ready1 = %d ready2 = %d\n", SIM_TIME, _mc->_id_ex->_ins, _mc->_id_ex->_src1, _mc->_id_ex->_src2, _mc->_gprReadyCycles[_mc->_id_ex->_src1], _mc->_gprReadyCycles[_mc->_id_ex->_src2]);
#endif
            _mc->_id_ex->_forwardSrc1 = 0;
            _mc->_id_ex->_forwardSrc2 = 0;
            _mc->_id_ex->_ins = 0;
            _mc->_id_ex->_bdslot = 0;
            _mc->_isInterlock = TRUE;
            _mc->_id_ex->Dec(_mc, _mc->_ex_mem, _mc->_mem_wb, _mc->_id_ex->_ins);
         } else if (!_mc->_id_ex->_isIllegalOp) {
legal_op:
            if (_mc->_id_ex->_writeREG) {
               _mc->_gprReadyCycles[_mc->_id_ex->_decodedDST] = 3;
               _mc->_gprForwardedReadyCycles[_mc->_id_ex->_decodedDST] = ready_cycles;
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> Set forwarded ready cycles of %d to %d\n", SIM_TIME, _mc->_id_ex->_decodedDST, ready_cycles);
#endif
            }
            if (_mc->_id_ex->_writeFREG) {
               _mc->_fprReadyCycles[_mc->_id_ex->_decodedDST >> 1] = 3;
               _mc->_fprForwardedReadyCycles[_mc->_id_ex->_decodedDST >> 1] = ready_cycles;
            }
            if (_mc->_id_ex->_loWPort) {
               _mc->_gprReadyCycles[LO] = 3;
               _mc->_gprForwardedReadyCycles[LO] = ready_cycles;
            }
            if (_mc->_id_ex->_hiWPort) {
               _mc->_gprReadyCycles[HI] = 3;
               _mc->_gprForwardedReadyCycles[HI] = ready_cycles;
            }
#ifdef MIPC_DEBUG
         fprintf(_mc->_debugLog, "<%llu> Decoded instruction %#x correctly\n", SIM_TIME, _mc->_id_ex->_ins);
#endif         
         } else {
            // TODO
         }
      } else {
         _mc->_id_ex->_ins = 0;

         _mc->_id_ex->Dec(_mc, _mc->_ex_mem, _mc->_mem_wb, _mc->_id_ex->_ins);
      }
   }
}
