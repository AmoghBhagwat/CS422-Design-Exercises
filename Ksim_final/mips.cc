#include "mips.h"
#include <assert.h>
#include "mips-irix5.h"

Mipc::Mipc (Mem *m) : _l('M')
{
   _mem = m;
   _sys = new MipcSysCall (this);	// Allocate syscall layer

#ifdef MIPC_DEBUG
   _debugLog = fopen("mipc.debug", "w");
   assert(_debugLog != NULL);
#endif
   
   Reboot (ParamGetString ("Mipc.BootROM"));
}

Mipc::~Mipc (void)
{

}

void 
Mipc::MainLoop (void)
{
   LL addr;
   unsigned int ins;	// Local instruction register

   Assert (_boot, "Mipc::MainLoop() called without boot?");

   _nfetched = 0;

   while (!_sim_exit) {
      AWAIT_P_PHI0;
      Bool stall = _isStall;

      AWAIT_P_PHI1;
      if (!stall) {
         addr = _pc;
         ins = _mem->BEGetWord(addr, _mem->Read(addr & ~(LL)0x7));
#ifdef MIPC_DEBUG
         fprintf(_debugLog, "<%llu> Fetched instruction %#x at PC %#x\n", SIM_TIME, ins, addr);
#endif
         _if_id->_prevIns = _if_id->_ins;
         _if_id->_prevPc = _if_id->_pc;
         _if_id->_ins = ins;
         _if_id->_pc = addr;
         _pc = _pc + 4;
         _nfetched++;
      }
   }

   MipcDumpstats();
   Log::CloseLog();
   
#ifdef MIPC_DEBUG
   assert(_debugLog != NULL);
   fclose(_debugLog);
#endif

   exit(0);
}

void
Mipc::MipcDumpstats()
{
  Log l('*');
  l.startLogging = 0;

  l.print ("");
  l.print ("************************************************************");
  l.print ("");
  l.print ("Number of instructions: %llu", _nfetched);
  l.print ("Number of simulated cycles: %llu", SIM_TIME);
  l.print ("CPI: %.2f", ((double)SIM_TIME)/_nfetched);
  l.print ("Int Conditional Branches: %llu", _ex_mem->_num_cond_br);
  l.print ("Jump and Link: %llu", _ex_mem->_num_jal);
  l.print ("Jump Register: %llu", _ex_mem->_num_jr);
  l.print ("Number of fp instructions: %llu", _fpinst);
  l.print ("Number of loads: %llu", _ex_mem->_num_load);
  l.print ("Number of syscall emulated loads: %llu", _sys->_num_load);
  l.print ("Number of stores: %llu", _ex_mem->_num_store);
  l.print ("Number of syscall emulated stores: %llu", _sys->_num_store);
  l.print ("");

}

void 
Mipc::fake_syscall (unsigned int ins)
{
   _sys->pc = _pc;
   _sys->quit = 0;
   _sys->EmulateSysCall ();
   if (_sys->quit)
      _sim_exit = 1;
}

/*------------------------------------------------------------------------
 *
 *  Mipc::Reboot --
 *
 *   Reset processor state
 *
 *------------------------------------------------------------------------
 */
void 
Mipc::Reboot (char *image)
{
   FILE *fp;
   Log l('*');

   _boot = 0;

   if (image) {
      _boot = 1;
      printf ("Executing %s\n", image);
      fp = fopen (image, "r");
      if (!fp) {
	 fatal_error ("Could not open `%s' for booting host!", image);
      }
      _mem->ReadImage(fp);
      fclose (fp);

      // Reset state
      _ins = 0;

      _num_load = 0;
      _num_store = 0;
      _fpinst = 0;
      _num_cond_br = 0;
      _num_jal = 0;
      _num_jr = 0;

      _lastbdslot = 0;
      _bdslot = 0;
      _btaken = 0;
      _btgt = 0xdeadbeef;
      _sim_exit = 0;

      for (int i = 0; i < 34; i++) _gprReadyCycles[i] = 0;
      for (int i = 0; i < 16; i++) _fprReadyCycles[i] = 0;
      for (int i = 0; i < 34; i++) _gprForwardedReadyCycles[i] = 0;
      for (int i = 0; i < 16; i++) _fprForwardedReadyCycles[i] = 0;

      _isStall = FALSE;
      _isInterlock = FALSE;

      _if_id = new IF_ID_Register();
      _id_ex = new ID_EX_Register();
      _ex_mem = new EX_MEM_Register();
      _mem_wb = new MEM_WB_Register();
      
      _pc = ParamGetInt ("Mipc.BootPC");	// Boom! GO
   }
}

LL
MipcSysCall::GetDWord(LL addr)
{
   _num_load++;      
   return m->Read (addr);
}

void
MipcSysCall::SetDWord(LL addr, LL data)
{
  
   m->Write (addr, data);
   _num_store++;
}

Word 
MipcSysCall::GetWord (LL addr) 
{ 
  
   _num_load++;   
   return m->BEGetWord (addr, m->Read (addr & ~(LL)0x7)); 
}

void 
MipcSysCall::SetWord (LL addr, Word data) 
{ 
  
   m->Write (addr & ~(LL)0x7, m->BESetWord (addr, m->Read(addr & ~(LL)0x7), data)); 
   _num_store++;
}
  
void 
MipcSysCall::SetReg (int reg, LL val) 
{ 
   _ms->_gpr[reg] = val; 
}

LL 
MipcSysCall::GetReg (int reg) 
{
   return _ms->_gpr[reg]; 
}

LL
MipcSysCall::GetTime (void)
{
  return SIM_TIME;
}

IF_ID_Register::IF_ID_Register() {
   this->_ins = 0;
   this->_pc = 0;
}

ID_EX_Register::ID_EX_Register() {
   this->_ins = 0;
   this->_pc = 0;
   this->_prevIns = 0;
   this->_prevPc = 0;
 
   this->_decodedSRC1 = 0;
   this->_decodedSRC2 = 0;
   this->_decodedDST = 0;
   this->_subregOperand = 0;
   this->_memory_addr_reg = 0;
   this->_opResultHi = 0;
   this->_opResultLo = 0;
   this->_memControl = FALSE;
   this->_writeREG = FALSE;
   this->_writeFREG = FALSE;
   this->_branchOffset = 0;
   this->_hiWPort = FALSE;
   this->_loWPort = FALSE;
   this->_decodedShiftAmt = 0;

   this->_src1 = 0;
   this->_src2 = 0;
   this->_bdslot = 0;
   this->_btgt = 0xdeadbeef;

   this->_forwardSrc1 = 0;
   this->_forwardSrc2 = 0;

   this->_isSyscall = FALSE;
   this->_isIllegalOp = FALSE;

   this->_decodedSRC3 = 0;
   this->_src3 = 0;
   this->_forwardSrc3 = 0;

   this->_opControl = EX_MEM_Register::func_sll;
   this->_memOp = 0;
}

EX_MEM_Register::EX_MEM_Register() {
   this->_ins = 0;
   this->_pc = 0;
 
   this->_decodedSRC1 = 0;
   this->_decodedSRC2 = 0;
   this->_decodedDST = 0;
   this->_subregOperand = 0;
   this->_memory_addr_reg = 0;
   this->_opResultHi = 0;
   this->_opResultLo = 0;
   this->_memControl = FALSE;
   this->_writeREG = FALSE;
   this->_writeFREG = FALSE;
   this->_branchOffset = 0;
   this->_hiWPort = FALSE;
   this->_loWPort = FALSE;
   this->_decodedShiftAmt = 0;

   this->_hi = 0;
   this->_lo = 0;
   this->_btaken = 0;
   this->_bdslot = 0;
   this->_btgt = 0xdeadbeef;

   this->_isSyscall = 0;
   this->_isIllegalOp = 0;

   this->_carryForward = 0;
   this->_src3 = 0;
   this->_decodedSRC3 = 0;

   this->_opControl = func_sll; // NOP
   this->_memOp = 0;

   this->_num_cond_br = 0;
   this->_num_jal = 0;
   this->_num_jr = 0;
   this->_num_load = 0;
   this->_num_store = 0;
}

MEM_WB_Register::MEM_WB_Register() {
   this->_ins = 0;
   this->_pc = 0;
 
   this->_decodedSRC1 = 0;
   this->_decodedSRC2 = 0;
   this->_decodedDST = 0;
   this->_subregOperand = 0;
   this->_memory_addr_reg = 0;
   this->_opResultHi = 0;
   this->_opResultLo = 0;
   this->_memControl = FALSE;
   this->_writeREG = FALSE;
   this->_writeFREG = FALSE;
   this->_branchOffset = 0;
   this->_hiWPort = FALSE;
   this->_loWPort = FALSE;
   this->_decodedShiftAmt = 0;

   this->_hi = 0;
   this->_lo = 0;
   this->_bdslot = 0;
   this->_btgt = 0xdeadbeef;

   this->_isSyscall = FALSE;
   this->_isIllegalOp = FALSE;

   this->_decodedSRC3 = 0;
}
