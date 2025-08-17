#ifndef __MIPS_H__
#define __MIPS_H__

#include "sim.h"

class Mipc;
class MipcSysCall;
class SysCall;

typedef unsigned Bool;
#define TRUE 1
#define FALSE 0
#define HI 32
#define LO 33

#if BYTE_ORDER == LITTLE_ENDIAN

#define FP_TWIDDLE 0

#else

#define FP_TWIDDLE 1

#endif

#include "mem.h"
#include "../../common/syscall.h"
#include "queue.h"

#define MIPC_DEBUG 1

class IF_ID_Register;
class ID_EX_Register;
class EX_MEM_Register;
class MEM_WB_Register;

class IF_ID_Register {
public:
   unsigned int _ins;
   unsigned int _pc;
   unsigned int _prevIns;
   unsigned int _prevPc;

   IF_ID_Register();
};

class ID_EX_Register {
public:
   unsigned int _ins;
   unsigned int _pc;
   unsigned int _prevIns;
   unsigned int _prevPc;

   signed int	_decodedSRC1, _decodedSRC2;	// Reg fetch output (source values)
   unsigned	_decodedDST;			// Decoder output (dest reg no)
   unsigned 	_subregOperand;			// Needed for lwl and lwr
   unsigned	_memory_addr_reg;				// Memory address register
   unsigned	_opResultHi, _opResultLo;	// Result of operation
   Bool 	_memControl;			// Memory instruction?
   Bool		_writeREG, _writeFREG;		// WB control
   signed int	_branchOffset;
   Bool 	_hiWPort, _loWPort;		// WB control
   unsigned	_decodedShiftAmt;		// Shift amount

   unsigned _src1, _src2;
   int 		_bdslot;				// 1 if the next ins is delay slot
   unsigned int	_btgt;				// branch target

   int  _forwardSrc1;
   int  _forwardSrc2;

   Bool _isSyscall;
   Bool _isIllegalOp;

   // SRC3 is needed for instructions like sw $1, x($2) - src3 is $1 
   signed int _decodedSRC3;
   unsigned _src3;
   int  _forwardSrc3;

   void (*_opControl)(EX_MEM_Register*, unsigned);
   void (*_memOp)(Mipc*, MEM_WB_Register*);

   ID_EX_Register();
   unsigned int  Dec (Mipc* _mc, EX_MEM_Register* _ex_mem, MEM_WB_Register* _mem_wb, unsigned int ins);			// Decoder function
};

class EX_MEM_Register {
public:
   unsigned int _ins;
   unsigned int _pc;

   signed int	_decodedSRC1, _decodedSRC2;	// Reg fetch output (source values)
   unsigned	_decodedDST;			// Decoder output (dest reg no)
   unsigned 	_subregOperand;			// Needed for lwl and lwr
   unsigned	_memory_addr_reg;				// Memory address register
   unsigned	_opResultHi, _opResultLo;	// Result of operation
   Bool 	_memControl;			// Memory instruction?
   Bool		_writeREG, _writeFREG;		// WB control
   signed int	_branchOffset;
   Bool 	_hiWPort, _loWPort;		// WB control
   unsigned	_decodedShiftAmt;		// Shift amount

   unsigned int _hi, _lo; 			// mult, div destination
   int 		_btaken; 			// taken branch (1 if taken, 0 if fall-through)
   int 		_bdslot;				// 1 if the next ins is delay slot
   unsigned int	_btgt;				// branch target

   unsigned int _src3;
   int  _forwardSrc3;

   Bool _isSyscall;
   Bool _isIllegalOp;

   unsigned int _carryForward; // executor wants to write to MEM-WB register also, in this case store which register to overwrite from EX-MEM

   // SRC3 is needed for instructions like sw $1, x($2) - src3 is $1 
   signed int _decodedSRC3;

   unsigned int 	_gprForward[34];		// store forwarded register file (bypassing)

   union {
      unsigned int l[2];
      float f[2];
      double d;
   } _fprForward[16];					// floating-point registers (paired)

   void (*_opControl)(EX_MEM_Register*, unsigned);
   void (*_memOp)(Mipc*, MEM_WB_Register*);

   LL	_num_cond_br;
   LL	_num_jal;
   LL	_num_jr;
   LL   _num_load;
   LL   _num_store;

   EX_MEM_Register();

   // EXE stage definitions
   static void func_add_addu (EX_MEM_Register*, unsigned);
   static void func_and (EX_MEM_Register*, unsigned);
   static void func_nor (EX_MEM_Register*, unsigned);
   static void func_or (EX_MEM_Register*, unsigned);
   static void func_sll (EX_MEM_Register*, unsigned);
   static void func_sllv (EX_MEM_Register*, unsigned);
   static void func_slt (EX_MEM_Register*, unsigned);
   static void func_sltu (EX_MEM_Register*, unsigned);
   static void func_sra (EX_MEM_Register*, unsigned);
   static void func_srav (EX_MEM_Register*, unsigned);
   static void func_srl (EX_MEM_Register*, unsigned);
   static void func_srlv (EX_MEM_Register*, unsigned);
   static void func_sub_subu (EX_MEM_Register*, unsigned);
   static void func_xor (EX_MEM_Register*, unsigned);
   static void func_div (EX_MEM_Register*, unsigned);
   static void func_divu (EX_MEM_Register*, unsigned);
   static void func_mfhi (EX_MEM_Register*, unsigned);
   static void func_mflo (EX_MEM_Register*, unsigned);
   static void func_mthi (EX_MEM_Register*, unsigned);
   static void func_mtlo (EX_MEM_Register*, unsigned);
   static void func_mult (EX_MEM_Register*, unsigned);
   static void func_multu (EX_MEM_Register*, unsigned);
   static void func_jalr (EX_MEM_Register*, unsigned);
   static void func_jr (EX_MEM_Register*, unsigned);
   static void func_await_break (EX_MEM_Register*, unsigned);
   static void func_syscall (EX_MEM_Register*, unsigned);
   static void func_addi_addiu (EX_MEM_Register*, unsigned);
   static void func_andi (EX_MEM_Register*, unsigned);
   static void func_lui (EX_MEM_Register*, unsigned);
   static void func_ori (EX_MEM_Register*, unsigned);
   static void func_slti (EX_MEM_Register*, unsigned);
   static void func_sltiu (EX_MEM_Register*, unsigned);
   static void func_xori (EX_MEM_Register*, unsigned);
   static void func_beq (EX_MEM_Register*, unsigned);
   static void func_bgez (EX_MEM_Register*, unsigned);
   static void func_bgezal (EX_MEM_Register*, unsigned);
   static void func_bltzal (EX_MEM_Register*, unsigned);
   static void func_bltz (EX_MEM_Register*, unsigned);
   static void func_bgtz (EX_MEM_Register*, unsigned);
   static void func_blez (EX_MEM_Register*, unsigned);
   static void func_bne (EX_MEM_Register*, unsigned);
   static void func_j (EX_MEM_Register*, unsigned);
   static void func_jal (EX_MEM_Register*, unsigned);
   static void func_lb (EX_MEM_Register*, unsigned);
   static void func_lbu (EX_MEM_Register*, unsigned);
   static void func_lh (EX_MEM_Register*, unsigned);
   static void func_lhu (EX_MEM_Register*, unsigned);
   static void func_lwl (EX_MEM_Register*, unsigned);
   static void func_lw (EX_MEM_Register*, unsigned);
   static void func_lwr (EX_MEM_Register*, unsigned);
   static void func_lwc1 (EX_MEM_Register*, unsigned);
   static void func_swc1 (EX_MEM_Register*, unsigned);
   static void func_sb (EX_MEM_Register*, unsigned);
   static void func_sh (EX_MEM_Register*, unsigned);
   static void func_swl (EX_MEM_Register*, unsigned);
   static void func_sw (EX_MEM_Register*, unsigned);
   static void func_swr (EX_MEM_Register*, unsigned);
   static void func_mtc1 (EX_MEM_Register*, unsigned);
   static void func_mfc1 (EX_MEM_Register*, unsigned);
};

class MEM_WB_Register {
public:
   unsigned int _ins;
   unsigned int _pc;

   signed int	_decodedSRC1, _decodedSRC2;	// Reg fetch output (source values)
   unsigned	_decodedDST;			// Decoder output (dest reg no)
   unsigned 	_subregOperand;			// Needed for lwl and lwr
   unsigned	_memory_addr_reg;				// Memory address register
   unsigned	_opResultHi, _opResultLo;	// Result of operation
   Bool 	_memControl;			// Memory instruction?
   Bool		_writeREG, _writeFREG;		// WB control
   signed int	_branchOffset;
   Bool 	_hiWPort, _loWPort;		// WB control
   unsigned	_decodedShiftAmt;		// Shift amount

   unsigned int _hi, _lo; 			// mult, div destination
   Bool _isSyscall;
   Bool _isIllegalOp;

   // SRC3 is needed for instructions like sw $1, x($2) - src3 is $1 
   signed int _decodedSRC3;
   
   int 		_bdslot;				// 1 if the next ins is delay slot
   unsigned int	_btgt;				// branch target

   unsigned int 	_gprForward[34];		// store forwarded register file (bypassing)

   MEM_WB_Register();

   void (*_memOp)(Mipc*, MEM_WB_Register*);

   // MEM stage definitions
   static void mem_lb (Mipc*, MEM_WB_Register*);
   static void mem_lbu (Mipc*, MEM_WB_Register*);
   static void mem_lh (Mipc*, MEM_WB_Register*);
   static void mem_lhu (Mipc*, MEM_WB_Register*);
   static void mem_lwl (Mipc*, MEM_WB_Register*);
   static void mem_lw (Mipc*, MEM_WB_Register*);
   static void mem_lwr (Mipc*, MEM_WB_Register*);
   static void mem_lwc1 (Mipc*, MEM_WB_Register*);
   static void mem_swc1 (Mipc*, MEM_WB_Register*);
   static void mem_sb (Mipc*, MEM_WB_Register*);
   static void mem_sh (Mipc*, MEM_WB_Register*);
   static void mem_swl (Mipc*, MEM_WB_Register*);
   static void mem_sw (Mipc*, MEM_WB_Register*);
   static void mem_swr (Mipc*, MEM_WB_Register*);
};

class Mipc : public SimObject {
public:
   Mipc (Mem *m);
   ~Mipc ();
  
   FAKE_SIM_TEMPLATE;

   MipcSysCall *_sys;		// Emulated system call layer

   void dumpregs (void);	// Dumps current register state

   void Reboot (char *image = NULL);
				// Restart processor.
				// "image" = file name for new memory
				// image if any.

   void MipcDumpstats();			// Prints simulation statistics
   void fake_syscall (unsigned int ins);	// System call interface

   /* processor state */
   unsigned int _ins;   // instruction register

   unsigned int 	_gpr[32];		// general-purpose integer registers

   union {
      unsigned int l[2];
      float f[2];
      double d;
   } _fpr[16];					// floating-point registers (paired)

   unsigned int _gprReadyCycles[34]; // 32 is hi, 33 is lo
   unsigned int _fprReadyCycles[16];
   unsigned int _gprForwardedReadyCycles[34]; // 32 is hi, 33 is lo
   unsigned int _fprForwardedReadyCycles[16];

   unsigned int _hi, _lo; 			// mult, div destination
   unsigned int	_pc;				// Program counter
   unsigned int _lastbdslot;			// branch delay state
   unsigned int _boot;				// boot code loaded?

   int 		_btaken; 			// taken branch (1 if taken, 0 if fall-through)
   int 		_bdslot;				// 1 if the next ins is delay slot
   unsigned int	_btgt;				// branch target

   Bool		_isSyscall;			// 1 if system call
   Bool		_isIllegalOp;			// 1 if illegal opcode
   Bool     _isStall;
   Bool     _isInterlock;

   // Simulation statistics counters

   LL	_nfetched;
   LL	_num_cond_br;
   LL	_num_jal;
   LL	_num_jr;
   LL   _num_load;
   LL   _num_store;
   LL   _fpinst;

   Mem	*_mem;	// attached memory (not a cache)

   Log	_l;
   int  _sim_exit;		// 1 on normal termination

   // Pipeline registers
   IF_ID_Register* _if_id;
   ID_EX_Register* _id_ex;
   EX_MEM_Register* _ex_mem;
   MEM_WB_Register* _mem_wb;

   FILE *_debugLog;
};


// Emulated system call interface

class MipcSysCall : public SysCall {
public:

   MipcSysCall (Mipc *ms) {

      char buf[1024];
      m = ms->_mem;
      _ms = ms;
      _num_load = 0;
      _num_store = 0;
   };

   ~MipcSysCall () { };

   LL GetDWord (LL addr);
   void SetDWord (LL addr, LL data);

   Word GetWord (LL addr);
   void SetWord (LL addr, Word data);
  
   void SetReg (int reg, LL val);
   LL GetReg (int reg);
   LL GetTime (void);

private:

   Mipc *_ms;
};
#endif /* __MIPS_H__ */
