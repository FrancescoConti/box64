#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <setjmp.h>

#include "debug.h"
#include "box64context.h"
#include "dynarec.h"
#include "emu/x64emu_private.h"
#include "tools/bridge_private.h"
#include "x64run.h"
#include "x64emu.h"
#include "box64stack.h"
#include "callback.h"
#include "emu/x64run_private.h"
#include "x64trace.h"
#include "threads.h"
#ifdef DYNAREC
#include "dynablock.h"
#include "dynablock_private.h"
#endif

#ifdef DYNAREC
#ifdef ARM
void arm_prolog(x64emu_t* emu, void* addr) EXPORTDYN;
void arm_epilog() EXPORTDYN;
void arm_epilog_fast() EXPORTDYN;
#endif
#endif

#ifdef DYNAREC
#ifdef HAVE_TRACE
uintptr_t getX64Address(dynablock_t* db, uintptr_t arm_addr);
#endif
void* LinkNext(x64emu_t* emu, uintptr_t addr, void* x2)
{
    #ifdef HAVE_TRACE
    if(!addr) {
        x2-=8;  // actual PC is 2 instructions ahead
        dynablock_t* db = FindDynablockFromNativeAddress(x2);
        printf_log(LOG_NONE, "Warning, jumping to NULL address from %p (db=%p, x86addr=%p)\n", x2, db, db?(void*)getX86Address(db, (uintptr_t)x2):NULL);
    }
    #endif
    dynablock_t* current = NULL;
    void * jblock;
    dynablock_t* block = DBGetBlock(emu, addr, 1, &current);
    if(!block) {
        // no block, let link table as is...
        //tableupdate(arm_epilog, addr, table);
        return arm_epilog;
    }
    if(!block->done) {
        // not finished yet... leave linker
        //tableupdate(arm_linker, addr, table);
        return arm_epilog;
    }
    if(!(jblock=block->block)) {
        // null block, but done: go to epilog, no linker here
        return arm_epilog;
    }
    //dynablock_t *father = block->father?block->father:block;
    return jblock;
}
#endif

void DynaCall(x64emu_t* emu, uintptr_t addr)
{
    // prepare setjump for signal handling
    emu_jmpbuf_t *ejb = NULL;
    int jmpbuf_reset = 0;
    if(emu->type == EMUTYPE_MAIN) {
        ejb = GetJmpBuf();
        if(!ejb->jmpbuf_ok) {
            ejb->emu = emu;
            ejb->jmpbuf_ok = 1;
            jmpbuf_reset = 1;
            if(setjmp((struct __jmp_buf_tag*)ejb->jmpbuf)) {
                printf_log(LOG_DEBUG, "Setjmp DynaCall, fs=0x%x\n", ejb->emu->segs[_FS]);
                addr = R_RIP;   // not sure if it should still be inside DynaCall!
            }
        }
    }
#ifdef DYNAREC
    if(!box86_dynarec)
#endif
        EmuCall(emu, addr);
#ifdef DYNAREC
    else {
        uint64_t old_rsp = R_RSP;
        uint64_t old_rbx = R_RBX;
        uint64_t old_rdi = R_RDI;
        uint64_t old_rsi = R_RSI;
        uint64_t old_rbp = R_RBP;
        uint64_t old_rip = R_RIP;
        PushExit(emu);
        R_RIP = addr;
        emu->df = d_none;
        dynablock_t* block = NULL;
        dynablock_t* current = NULL;
        while(!emu->quit) {
            block = DBGetBlock(emu, R_RIP, 1, &current);
            current = block;
            if(!block || !block->block || !block->done) {
                // no block, of block doesn't have DynaRec content (yet, temp is not null)
                // Use interpreter (should use single instruction step...)
                dynarec_log(LOG_DEBUG, "%04d|Calling Interpretor @%p, emu=%p\n", GetTID(), (void*)R_RIP, emu);
                Run(emu, 1);
            } else {
                dynarec_log(LOG_DEBUG, "%04d|Calling DynaRec Block @%p (%p) of %d x86 instructions (father=%p) emu=%p\n", GetTID(), (void*)R_RIP, block->block, block->isize ,block->father, emu);
                CHECK_FLAGS(emu);
                // block is here, let's run it!
                #ifdef ARM
                arm_prolog(emu, block->block);
                #endif
            }
            if(emu->fork) {
                int forktype = emu->fork;
                emu->quit = 0;
                emu->fork = 0;
                emu = x86emu_fork(emu, forktype);
                if(emu->type == EMUTYPE_MAIN) {
                    ejb = GetJmpBuf();
                    ejb->emu = emu;
                    ejb->jmpbuf_ok = 1;
                    jmpbuf_reset = 1;
                    if(setjmp((struct __jmp_buf_tag*)ejb->jmpbuf)) {
                        printf_log(LOG_DEBUG, "Setjmp inner DynaCall, fs=0x%x\n", ejb->emu->segs[_FS]);
                        addr = R_RIP;
                    }
                }
            }
        }
        emu->quit = 0;  // reset Quit flags...
        emu->df = d_none;
        if(emu->quitonlongjmp && emu->longjmp) {
            emu->longjmp = 0;   // don't change anything because of the longjmp
        } else {
            R_RBX = old_rbx;
            R_RDI = old_rdi;
            R_RSI = old_rsi;
            R_RBP = old_rbp;
            R_RSP = old_rsp;
            R_RIP = old_rip;  // and set back instruction pointer
        }
    }
#endif
    // clear the setjmp
    if(ejb && jmpbuf_reset)
        ejb->jmpbuf_ok = 0;
}

int DynaRun(x64emu_t* emu)
{
    // prepare setjump for signal handling
    emu_jmpbuf_t *ejb = NULL;
#ifdef DYNAREC
    int jmpbuf_reset = 1;
#endif
    if(emu->type == EMUTYPE_MAIN) {
        ejb = GetJmpBuf();
        if(!ejb->jmpbuf_ok) {
            ejb->emu = emu;
            ejb->jmpbuf_ok = 1;
#ifdef DYNAREC
            jmpbuf_reset = 1;
#endif
            if(setjmp((struct __jmp_buf_tag*)ejb->jmpbuf))
                printf_log(LOG_DEBUG, "Setjmp DynaRun, fs=0x%x\n", ejb->emu->segs[_FS]);
        }
    }
#ifdef DYNAREC
    if(!box86_dynarec)
#endif
        return Run(emu, 0);
#ifdef DYNAREC
    else {
        dynablock_t* block = NULL;
        dynablock_t* current = NULL;
        while(!emu->quit) {
            block = DBGetBlock(emu, R_RIP, 1, &current);
            current = block;
            if(!block || !block->block || !block->done) {
                // no block, of block doesn't have DynaRec content (yet, temp is not null)
                // Use interpreter (should use single instruction step...)
                dynarec_log(LOG_DEBUG, "%04d|Running Interpretor @%p, emu=%p\n", GetTID(), (void*)R_RIP, emu);
                Run(emu, 1);
            } else {
                dynarec_log(LOG_DEBUG, "%04d|Running DynaRec Block @%p (%p) of %d x86 insts (father=%p) emu=%p\n", GetTID(), (void*)R_RIP, block->block, block->isize, block->father, emu);
                // block is here, let's run it!
                #ifdef ARM
                arm_prolog(emu, block->block);
                #endif
            }
            if(emu->fork) {
                int forktype = emu->fork;
                emu->quit = 0;
                emu->fork = 0;
                emu = x86emu_fork(emu, forktype);
                if(emu->type == EMUTYPE_MAIN) {
                    ejb = GetJmpBuf();
                    ejb->emu = emu;
                    ejb->jmpbuf_ok = 1;
                    jmpbuf_reset = 1;
                    if(setjmp((struct __jmp_buf_tag*)ejb->jmpbuf))
                        printf_log(LOG_DEBUG, "Setjmp inner DynaRun, fs=0x%x\n", ejb->emu->segs[_FS]);
                }
            }
        }
    }
    // clear the setjmp
    if(ejb && jmpbuf_reset)
        ejb->jmpbuf_ok = 0;
    return 0;
#endif
}