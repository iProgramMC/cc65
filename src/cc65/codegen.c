/*****************************************************************************/
/*                                                                           */
/*				   codegen.c				     */
/*                                                                           */
/*			      6502 code generator			     */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 1998-2001 Ullrich von Bassewitz                                       */
/*               Wacholderweg 14                                             */
/*               D-70597 Stuttgart                                           */
/* EMail:        uz@cc65.org                                                 */
/*                                                                           */
/*                                                                           */
/* This software is provided 'as-is', without any expressed or implied       */
/* warranty.  In no event will the authors be held liable for any damages    */
/* arising from the use of this software.                                    */
/*                                                                           */
/* Permission is granted to anyone to use this software for any purpose,     */
/* including commercial applications, and to alter it and redistribute it    */
/* freely, subject to the following restrictions:                            */
/*                                                                           */
/* 1. The origin of this software must not be misrepresented; you must not   */
/*    claim that you wrote the original software. If you use this software   */
/*    in a product, an acknowledgment in the product documentation would be  */
/*    appreciated but is not required.                                       */
/* 2. Altered source versions must be plainly marked as such, and must not   */
/*    be misrepresented as being the original software.                      */
/* 3. This notice may not be removed or altered from any source              */
/*    distribution.                                                          */
/*                                                                           */
/*****************************************************************************/



#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* common */
#include "check.h"
#include "version.h"
#include "xmalloc.h"
#include "xsprintf.h"

/* cc65 */
#include "asmcode.h"
#include "asmlabel.h"
#include "codeseg.h"
#include "cpu.h"
#include "dataseg.h"
#include "error.h"
#include "global.h"
#include "segments.h"
#include "util.h"
#include "codegen.h"



/*****************************************************************************/
/*	  			     Data     				     */
/*****************************************************************************/



/* Compiler relative stack pointer */
int oursp 	= 0;



/*****************************************************************************/
/*     				    Helpers   	  			     */
/*****************************************************************************/



static void typeerror (unsigned type)
/* Print an error message about an invalid operand type */
{
    Internal ("Invalid type in CF flags: %04X, type = %u", type, type & CF_TYPE);
}



static void CheckLocalOffs (unsigned Offs)
/* Check the offset into the stack for 8bit range */
{
    if (Offs >= 256) {
	/* Too many local vars */
	Error ("Too many local variables");
    }
}



static char* GetLabelName (unsigned flags, unsigned long label, unsigned offs)
{
    static char lbuf [128];		/* Label name */

    /* Create the correct label name */
    switch (flags & CF_ADDRMASK) {

	case CF_STATIC:
       	    /* Static memory cell */
	    sprintf (lbuf, "%s+%u", LocalLabelName (label), offs);
	    break;

	case CF_EXTERNAL:
	    /* External label */
	    sprintf (lbuf, "_%s+%u", (char*) label, offs);
	    break;

	case CF_ABSOLUTE:
	    /* Absolute address */
	    sprintf (lbuf, "$%04X", (unsigned)((label+offs) & 0xFFFF));
	    break;

	case CF_REGVAR:
	    /* Variable in register bank */
	    sprintf (lbuf, "regbank+%u", (unsigned)((label+offs) & 0xFFFF));
	    break;

	default:
	    Internal ("Invalid address flags");
    }

    /* Return a pointer to the static buffer */
    return lbuf;
}



const char* NumToStr (long Val)
/* Convert the given parameter converted to a string in a static buffer */
{
    static char Buf [64];
    sprintf (Buf, "$%04X", (unsigned) (Val & 0xFFFF));
    return Buf;
}



const char* ByteToStr (unsigned Val)
/* Convert the given byte parameter converted to a string in a static buffer */
{
    static char Buf [16];
    sprintf (Buf, "$%02X", Val & 0xFF);
    return Buf;
}



const char* WordToStr (unsigned Val)
/* Convert the given word parameter converted to a string in a static buffer */
{
    static char Buf [16];
    sprintf (Buf, "$%04X", Val & 0xFFFF);
    return Buf;
}



const char* DWordToStr (unsigned long Val)
/* Convert the given dword parameter converted to a string in a static buffer */
{
    static char Buf [16];
    sprintf (Buf, "$%08lX", Val & 0xFFFFFFFF);
    return Buf;
}



/*****************************************************************************/
/*		    	      Pre- and postamble			     */
/*****************************************************************************/



void g_preamble (void)
/* Generate the assembler code preamble */
{
    /* Create a new segment list */
    PushSegments (0);

    /* Identify the compiler version */
    AddTextLine (";");
    AddTextLine ("; File generated by cc65 v %u.%u.%u",
	   	 VER_MAJOR, VER_MINOR, VER_PATCH);
    AddTextLine (";");

    /* Insert some object file options */
    AddTextLine ("\t.fopt\t\tcompiler,\"cc65 v %u.%u.%u\"",
	   	    VER_MAJOR, VER_MINOR, VER_PATCH);

    /* If we're producing code for some other CPU, switch the command set */
    if (CPU == CPU_65C02) {
	AddTextLine ("\t.pc02");
    }

    /* Allow auto import for runtime library routines */
    AddTextLine ("\t.autoimport\ton");

    /* Switch the assembler into case sensitive mode */
    AddTextLine ("\t.case\t\ton");

    /* Tell the assembler if we want to generate debug info */
    AddTextLine ("\t.debuginfo\t%s", (DebugInfo != 0)? "on" : "off");

    /* Import the stack pointer for direct auto variable access */
    AddTextLine ("\t.importzp\tsp, sreg, regsave, regbank, tmp1, ptr1");

    /* Define long branch macros */
    AddTextLine ("\t.macpack\tlongbranch");
}



void g_fileinfo (const char* Name, unsigned long Size, unsigned long MTime)
/* If debug info is enabled, place a file info into the source */
{
    if (DebugInfo) {
       	AddTextLine ("\t.dbg\t\tfile, \"%s\", %lu, %lu", Name, Size, MTime);
    }
}



/*****************************************************************************/
/*  	   			Segment support				     */
/*****************************************************************************/



void g_userodata (void)
/* Switch to the read only data segment */
{
    UseDataSeg (SEG_RODATA);
}



void g_usedata (void)
/* Switch to the data segment */
{
    UseDataSeg (SEG_DATA);
}



void g_usebss (void)
/* Switch to the bss segment */
{
    UseDataSeg (SEG_BSS);
}



static void OutputDataLine (DataSeg* S, const char* Format, ...)
/* Add a line to the current data segment */
{
    va_list ap;
    va_start (ap, Format);
    DS_AddLine (S, Format, ap);
    va_end (ap);
}



void g_segname (segment_t Seg, const char* Name)
/* Set the name of a segment */
{
    DataSeg* S;

    /* Remember the new name */
    NewSegName (Seg, Name);

    /* Emit a segment directive for the data style segments */
    switch (Seg) {
     	case SEG_RODATA: S = CS->ROData; break;
     	case SEG_DATA:   S = CS->Data;   break;
     	case SEG_BSS:    S = CS->BSS;    break;
	default:         S = 0;          break;
    }
    if (S) {
       	OutputDataLine (S, ".segment\t\"%s\"", Name);
    }
}



/*****************************************************************************/
/*     	       		 	     Code				     */
/*****************************************************************************/



unsigned sizeofarg (unsigned flags)
/* Return the size of a function argument type that is encoded in flags */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    return (flags & CF_FORCECHAR)? 1 : 2;

	case CF_INT:
	    return 2;

	case CF_LONG:
	    return 4;

	default:
	    typeerror (flags);
	    /* NOTREACHED */
	    return 2;
    }
}



int pop (unsigned flags)
/* Pop an argument of the given size */
{
    return oursp += sizeofarg (flags);
}



int push (unsigned flags)
/* Push an argument of the given size */
{
    return oursp -= sizeofarg (flags);
}



static unsigned MakeByteOffs (unsigned Flags, unsigned Offs)
/* The value in Offs is an offset to an address in a/x. Make sure, an object
 * of the type given in Flags can be loaded or stored into this address by
 * adding part of the offset to the address in ax, so that the remaining
 * offset fits into an index register. Return the remaining offset.
 */
{
    /* If the offset is too large for a byte register, add the high byte
     * of the offset to the primary. Beware: We need a special correction
     * if the offset in the low byte will overflow in the operation.
     */
    unsigned O = Offs & ~0xFFU;
    if ((Offs & 0xFF) > 256 - sizeofarg (Flags)) {
	/* We need to add the low byte also */
	O += Offs & 0xFF;
    }

    /* Do the correction if we need one */
    if (O != 0) {
     	g_inc (CF_INT | CF_CONST, O);
     	Offs -= O;
    }

    /* Return the new offset */
    return Offs;
}



/*****************************************************************************/
/*		  	Functions handling local labels			     */
/*****************************************************************************/



void g_defcodelabel (unsigned label)
/* Define a local code label */
{
    CS_AddLabel (CS->Code, LocalLabelName (label));
}



void g_defdatalabel (unsigned label)
/* Define a local data label */
{
    AddDataLine ("%s:", LocalLabelName (label));
}



/*****************************************************************************/
/*     	     	       Functions handling global labels			     */
/*****************************************************************************/



void g_defgloblabel (const char* Name)
/* Define a global label with the given name */
{
    /* Global labels are always data labels */
    AddDataLine ("_%s:", Name);
}



void g_defexport (const char* Name, int ZP)
/* Export the given label */
{
    if (ZP) {
       	AddTextLine ("\t.exportzp\t_%s", Name);
    } else {
     	AddTextLine ("\t.export\t\t_%s", Name);
    }
}



void g_defimport (const char* Name, int ZP)
/* Import the given label */
{
    if (ZP) {
       	AddTextLine ("\t.importzp\t_%s", Name);
    } else {
     	AddTextLine ("\t.import\t\t_%s", Name);
    }
}



/*****************************************************************************/
/*     		     Load functions for various registers		     */
/*****************************************************************************/



static void ldaconst (unsigned val)
/* Load a with a constant */
{
    AddCodeLine ("lda #$%02X", val & 0xFF);
}



static void ldxconst (unsigned val)
/* Load x with a constant */
{
    AddCodeLine ("ldx #$%02X", val & 0xFF);
}



static void ldyconst (unsigned val)
/* Load y with a constant */
{
    AddCodeLine ("ldy #$%02X", val & 0xFF);
}



/*****************************************************************************/
/*     			    Function entry and exit			     */
/*****************************************************************************/



/* Remember the argument size of a function. The variable is set by g_enter
 * and used by g_leave. If the functions gets its argument size by the caller
 * (variable param list or function without prototype), g_enter will set the
 * value to -1.
 */
static int funcargs;


void g_enter (unsigned flags, unsigned argsize)
/* Function prologue */
{
    if ((flags & CF_FIXARGC) != 0) {
	/* Just remember the argument size for the leave */
	funcargs = argsize;
    } else {
       	funcargs = -1;
	AddCode (OPC_ENTER, AM_IMP, 0, 0);
    }
}



void g_leave (void)
/* Function epilogue */
{
    /* How many bytes of locals do we have to drop? */
    int k = -oursp;

    /* If we didn't have a variable argument list, don't call leave */
    if (funcargs >= 0) {

     	/* Drop stackframe if needed */
     	k += funcargs;
       	if (k > 0) {
	    CheckLocalOffs (k);
	    AddCode (OPC_SPACE, AM_IMM, NumToStr (-k), 0);
     	}

    } else {

	if (k > 0) {
	    AddCode (OPC_SPACE, AM_IMM, NumToStr (-k), 0);
	}
	AddCode (OPC_LEAVE, AM_IMP, 0, 0);
    }

    /* Add the final rts */
    AddCode (OPC_RET, AM_IMP, 0, 0);
}



/*****************************************************************************/
/*   		       	      Register variables			     */
/*****************************************************************************/



void g_save_regvars (int RegOffs, unsigned Bytes)
/* Save register variables */
{
    /* Don't loop for up to two bytes */
    if (Bytes == 1) {

     	AddCodeLine ("lda regbank%+d", RegOffs);
       	AddCodeLine ("jsr pusha");

    } else if (Bytes == 2) {

       	AddCodeLine ("lda regbank%+d", RegOffs);
	AddCodeLine ("ldx regbank%+d", RegOffs+1);
       	AddCodeLine ("jsr pushax");

    } else {

     	/* More than two bytes - loop */
     	unsigned Label = GetLocalLabel ();
     	g_space (Bytes);
     	ldyconst (Bytes - 1);
     	ldxconst (Bytes);
     	g_defcodelabel (Label);
	AddCodeLine ("lda regbank%+d,x", RegOffs-1);
	AddCodeLine ("sta (sp),y");
     	AddCodeLine ("dey");
     	AddCodeLine ("dex");
     	AddCodeLine ("bne %s", LocalLabelName (Label));

    }

    /* We pushed stuff, correct the stack pointer */
    oursp -= Bytes;
}



void g_restore_regvars (int StackOffs, int RegOffs, unsigned Bytes)
/* Restore register variables */
{
    /* Calculate the actual stack offset and check it */
    StackOffs -= oursp;
    CheckLocalOffs (StackOffs);

    /* Don't loop for up to two bytes */
    if (Bytes == 1) {

     	ldyconst (StackOffs);
     	AddCodeLine ("lda (sp),y");
	AddCodeLine ("sta regbank%+d", RegOffs);

    } else if (Bytes == 2) {

     	ldyconst (StackOffs);
     	AddCodeLine ("lda (sp),y");
	AddCodeLine ("sta regbank%+d", RegOffs);
	AddCodeLine ("iny");
	AddCodeLine ("lda (sp),y");
	AddCodeLine ("sta regbank%+d", RegOffs+1);

    } else {

     	/* More than two bytes - loop */
     	unsigned Label = GetLocalLabel ();
     	ldyconst (StackOffs+Bytes-1);
     	ldxconst (Bytes);
     	g_defcodelabel (Label);
	AddCodeLine ("lda (sp),y");
	AddCodeLine ("sta regbank%+d,x", RegOffs-1);
	AddCodeLine ("dey");
	AddCodeLine ("dex");
	AddCodeLine ("bne %s", LocalLabelName (Label));

    }
}



/*****************************************************************************/
/*		       	     Fetching memory cells	   		     */
/*****************************************************************************/



void g_getimmed (unsigned Flags, unsigned long Val, unsigned Offs)
/* Load a constant into the primary register */
{
    if ((Flags & CF_CONST) != 0) {

     	/* Numeric constant */
     	switch (Flags & CF_TYPE) {

     	    case CF_CHAR:
     		if ((Flags & CF_FORCECHAR) != 0) {
		    AddCode (OPC_LDA, AM_IMM, ByteToStr (Val), 0);
     		    break;
     		}
     		/* FALL THROUGH */
     	    case CF_INT:
	        AddCode (OPC_LDAX, AM_IMM, WordToStr (Val), 0);
     		break;

     	    case CF_LONG:
	        AddCode (OPC_LDEAX, AM_IMM, DWordToStr (Val), 0);
     		break;

     	    default:
     		typeerror (Flags);
     		break;

     	}

    } else {

	/* Some sort of label, load it into the primary */
       	AddCode (OPC_LEA, AM_ABS, GetLabelName (Flags, Val, Offs), 0);

    }
}



void g_getstatic (unsigned flags, unsigned long label, unsigned offs)
/* Fetch an static memory cell into the primary register */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and generate the correct load operation */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
     	    if ((flags & CF_FORCECHAR) || (flags & CF_TEST)) {
     	        AddCodeLine ("lda %s", lbuf);	/* load A from the label */
       	    } else {
     	     	ldxconst (0);
     	     	AddCodeLine ("lda %s", lbuf);	/* load A from the label */
     	     	if (!(flags & CF_UNSIGNED)) {
     	     	    /* Must sign extend */
		    unsigned L = GetLocalLabel ();
     	     	    AddCodeLine ("bpl %s", LocalLabelName (L));
     	     	    AddCodeLine ("dex");
		    g_defcodelabel (L);
     	     	}
     	    }
     	    break;

     	case CF_INT:
     	    AddCodeLine ("lda %s", lbuf);
     	    if (flags & CF_TEST) {
     		AddCodeLine ("ora %s+1", lbuf);
     	    } else {
     		AddCodeLine ("ldx %s+1", lbuf);
     	    }
     	    break;

   	case CF_LONG:
     	    if (flags & CF_TEST) {
	     	AddCodeLine ("lda %s+3", lbuf);
		AddCodeLine ("ora %s+2", lbuf);
		AddCodeLine ("ora %s+1", lbuf);
		AddCodeLine ("ora %s+0", lbuf);
	    } else {
	     	AddCodeLine ("lda %s+3", lbuf);
	     	AddCodeLine ("sta sreg+1");
		AddCodeLine ("lda %s+2", lbuf);
		AddCodeLine ("sta sreg");
		AddCodeLine ("ldx %s+1", lbuf);
		AddCodeLine ("lda %s", lbuf);
	    }
	    break;

       	default:
       	    typeerror (flags);

    }
}



void g_getlocal (unsigned flags, int offs)
/* Fetch specified local object (local var). */
{
    offs -= oursp;
    CheckLocalOffs (offs);
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    if ((flags & CF_FORCECHAR) || (flags & CF_TEST)) {
		if (CPU == CPU_65C02 && offs == 0) {
		    AddCodeLine ("lda (sp)");
		} else {
		    ldyconst (offs);
		    AddCodeLine ("lda (sp),y");
		}
	    } else {
		ldyconst (offs);
		AddCodeLine ("ldx #$00");
		AddCodeLine ("lda (sp),y");
     	    	if ((flags & CF_UNSIGNED) == 0) {
		    unsigned L = GetLocalLabel();
     	    	    AddCodeLine ("bpl %s", LocalLabelName (L));
     	 	    AddCodeLine ("dex");
		    g_defcodelabel (L);
	 	}
	    }
	    break;

	case CF_INT:
	    CheckLocalOffs (offs + 1);
       	    if (flags & CF_TEST) {
	    	ldyconst (offs + 1);
	    	AddCodeLine ("lda (sp),y");
		AddCodeLine ("dey");
		AddCodeLine ("ora (sp),y");
	    } else {
		if (CodeSizeFactor > 180) {
	    	    ldyconst (offs + 1);
		    AddCodeLine ("lda (sp),y");
		    AddCodeLine ("tax");
		    AddCodeLine ("dey");
		    AddCodeLine ("lda (sp),y");
		} else {
		    if (offs) {
			ldyconst (offs+1);
       			AddCodeLine ("jsr ldaxysp");
		    } else {
			AddCodeLine ("jsr ldax0sp");
		    }
		}
	    }
	    break;

	case CF_LONG:
    	    if (offs) {
    	 	ldyconst (offs+3);
    	 	AddCodeLine ("jsr ldeaxysp");
    	    } else {
    	 	AddCodeLine ("jsr ldeax0sp");
    	    }
       	    break;

       	default:
    	    typeerror (flags);
    }
}



void g_getind (unsigned flags, unsigned offs)
/* Fetch the specified object type indirect through the primary register
 * into the primary register
 */
{
    /* If the offset is greater than 255, add the part that is > 255 to
     * the primary. This way we get an easy addition and use the low byte
     * as the offset
     */
    offs = MakeByteOffs (flags, offs);

    /* Handle the indirect fetch */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
       	    /* Character sized */
     	    if (offs) {
     		ldyconst (offs);
     	        if (flags & CF_UNSIGNED) {
     	     	    AddCodeLine ("jsr ldauidx");
       	     	} else {
     	     	    AddCodeLine ("jsr ldaidx");
     	     	}
     	    } else {
     	        if (flags & CF_UNSIGNED) {
     		    if (CodeSizeFactor > 330) {
     			AddCodeLine ("sta ptr1");
     			AddCodeLine ("stx ptr1+1");
			AddCodeLine ("ldy #$00");
     		     	AddCodeLine ("ldx #$00");
     			AddCodeLine ("lda (ptr1),y");
     		    } else {
     	     	        AddCodeLine ("jsr ldaui");
     		    }
     	     	} else {
       	     	    AddCodeLine ("jsr ldai");
     	     	}
       	    }
     	    break;

     	case CF_INT:
     	    if (flags & CF_TEST) {
     		ldyconst (offs);
     		AddCodeLine ("sta ptr1");
     		AddCodeLine ("stx ptr1+1");
     		AddCodeLine ("lda (ptr1),y");
     		AddCodeLine ("iny");
     		AddCodeLine ("ora (ptr1),y");
     	    } else {
     		if (offs == 0) {
     		    AddCodeLine ("jsr ldaxi");
     		} else {
     		    ldyconst (offs+1);
     		    AddCodeLine ("jsr ldaxidx");
     		}
     	    }
     	    break;

       	case CF_LONG:
     	    if (offs == 0) {
     		AddCodeLine ("jsr ldeaxi");
     	    } else {
     		ldyconst (offs+3);
     		AddCodeLine ("jsr ldeaxidx");
     	    }
     	    if (flags & CF_TEST) {
       		AddCodeLine ("jsr tsteax");
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_leasp (int offs)
/* Fetch the address of the specified symbol into the primary register */
{
    /* Calculate the offset relative to sp */
    offs -= oursp;

    /* Output code */
    AddCode (OPC_LEA, AM_STACK,	WordToStr (offs), 0);
}



void g_leavariadic (int Offs)
/* Fetch the address of a parameter in a variadic function into the primary
 * register
 */
{
    unsigned ArgSizeOffs;

    /* Calculate the offset relative to sp */
    Offs -= oursp;

    /* Get the offset of the parameter which is stored at sp+0 on function
     * entry and check if this offset is reachable with a byte offset.
     */
    CHECK (oursp <= 0);
    ArgSizeOffs = -oursp;
    CheckLocalOffs (ArgSizeOffs);

    /* Get the size of all parameters. */
    if (ArgSizeOffs == 0 && CPU == CPU_65C02) {
       	AddCodeLine ("lda (sp)");
    } else {
       	ldyconst (ArgSizeOffs);
       	AddCodeLine ("lda (sp),y");
    }

    /* Add the value of the stackpointer */
    if (CodeSizeFactor > 250) {
	unsigned L = GetLocalLabel();
       	AddCodeLine ("ldx sp+1");
       	AddCodeLine ("clc");
       	AddCodeLine ("adc sp");
       	AddCodeLine ("bcc %s", LocalLabelName (L));
       	AddCodeLine ("inx");
	g_defcodelabel (L);
    } else {
       	AddCodeLine ("jsr leaasp");
    }

    /* Add the offset to the primary */
    if (Offs > 0) {
	g_inc (CF_INT | CF_CONST, Offs);
    } else if (Offs < 0) {
	g_dec (CF_INT | CF_CONST, -Offs);
    }
}



/*****************************************************************************/
/*     	    		       Store into memory			     */
/*****************************************************************************/



void g_putstatic (unsigned flags, unsigned long label, unsigned offs)
/* Store the primary register into the specified static memory cell */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and generate the correct store operation */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
	    AddCode (OPC_STA, AM_ABS, lbuf, 0);
     	    break;

     	case CF_INT:
	    AddCode (OPC_STAX, AM_ABS, lbuf, 0);
     	    break;

     	case CF_LONG:
	    AddCode (OPC_STEAX, AM_ABS, lbuf, 0);
     	    break;

       	default:
       	    typeerror (flags);

    }
}



void g_putlocal (unsigned Flags, int Offs, long Val)
/* Put data into local object. */
{
    Offs -= oursp;
    CheckLocalOffs (Offs);
			      
    if (Flags & CF_CONST) {
	g_getimmed (Flags, Val, Offs);
    }

    switch (Flags & CF_TYPE) {

     	case CF_CHAR:
     	    AddCode (OPC_STA, AM_STACK, WordToStr (Offs), 0);
     	    break;

     	case CF_INT:
	    AddCode (OPC_STAX, AM_STACK, WordToStr (Offs), 0);
     	    break;

     	case CF_LONG:
	    AddCode (OPC_STAEAX, AM_STACK, WordToStr (Offs), 0);
     	    break;

       	default:
     	    typeerror (Flags);

    }
}



void g_putind (unsigned Flags, unsigned Offs)
/* Store the specified object type in the primary register at the address
 * on the top of the stack
 */
{
    /* We can handle offsets below $100 directly, larger offsets must be added
     * to the address. Since a/x is in use, best code is achieved by adding
     * just the high byte. Be sure to check if the low byte will overflow while
     * while storing.
     */
    if ((Offs & 0xFF) > 256 - sizeofarg (Flags | CF_FORCECHAR)) {

	/* Overflow - we need to add the low byte also */
	AddCodeLine ("ldy #$00");
	AddCodeLine ("clc");
	AddCodeLine ("pha");
	AddCodeLine ("lda #$%02X", Offs & 0xFF);
	AddCodeLine ("adc (sp),y");
	AddCodeLine ("sta (sp),y");
	AddCodeLine ("iny");
       	AddCodeLine ("lda #$%02X", (Offs >> 8) & 0xFF);
	AddCodeLine ("adc (sp),y");
	AddCodeLine ("sta (sp),y");
	AddCodeLine ("pla");

	/* Complete address is on stack, new offset is zero */
	Offs = 0;

    } else if ((Offs & 0xFF00) != 0) {

	/* We can just add the high byte */
	AddCodeLine ("ldy #$01");
	AddCodeLine ("clc");
     	AddCodeLine ("pha");
	AddCodeLine ("lda #$%02X", (Offs >> 8) & 0xFF);
	AddCodeLine ("adc (sp),y");
	AddCodeLine ("sta (sp),y");
	AddCodeLine ("pla");

	/* Offset is now just the low byte */
	Offs &= 0x00FF;
    }

    /* Check the size and determine operation */
    switch (Flags & CF_TYPE) {

     	case CF_CHAR:
     	    if (Offs) {
     	        ldyconst (Offs);
     	       	AddCodeLine ("jsr staspidx");
     	    } else {
     	     	AddCodeLine ("jsr staspp");
     	    }
     	    break;

     	case CF_INT:
     	    if (Offs) {
     	        ldyconst (Offs);
     	     	AddCodeLine ("jsr staxspidx");
     	    } else {
     	     	AddCodeLine ("jsr staxspp");
     	    }
     	    break;

     	case CF_LONG:
     	    if (Offs) {
     	        ldyconst (Offs);
     	     	AddCodeLine ("jsr steaxspidx");
     	    } else {
     	     	AddCodeLine ("jsr steaxspp");
     	    }
     	    break;

     	default:
     	    typeerror (Flags);

    }

    /* Pop the argument which is always a pointer */
    pop (CF_PTR);
}



/*****************************************************************************/
/*		      type conversion and similiar stuff		     */
/*****************************************************************************/



void g_toslong (unsigned flags)
/* Make sure, the value on TOS is a long. Convert if necessary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    if (flags & CF_UNSIGNED) {
		AddCodeLine ("jsr tosulong");
	    } else {
		AddCodeLine ("jsr toslong");
	    }
	    push (CF_INT);
	    break;

	case CF_LONG:
	    break;

	default:
	    typeerror (flags);
    }
}



void g_tosint (unsigned flags)
/* Make sure, the value on TOS is an int. Convert if necessary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    break;

	case CF_LONG:
	    AddCodeLine ("jsr tosint");
	    pop (CF_INT);
	    break;

	default:
	    typeerror (flags);
    }
}



void g_reglong (unsigned flags)
/* Make sure, the value in the primary register a long. Convert if necessary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    if (flags & CF_UNSIGNED) {
	    	if (CodeSizeFactor >= 200) {
	    	    ldyconst (0);
	    	    AddCodeLine ("sty sreg");
		    AddCodeLine ("sty sreg+1");
	    	} else {
	       	    AddCodeLine ("jsr axulong");
	    	}
	    } else {
	    	AddCodeLine ("jsr axlong");
	    }
	    break;

	case CF_LONG:
	    break;

	default:
	    typeerror (flags);
    }
}



unsigned g_typeadjust (unsigned lhs, unsigned rhs)
/* Adjust the integer operands before doing a binary operation. lhs is a flags
 * value, that corresponds to the value on TOS, rhs corresponds to the value
 * in (e)ax. The return value is the the flags value for the resulting type.
 */
{
    unsigned ltype, rtype;
    unsigned result;

    /* Get the type spec from the flags */
    ltype = lhs & CF_TYPE;
    rtype = rhs & CF_TYPE;

    /* Check if a conversion is needed */
    if (ltype == CF_LONG && rtype != CF_LONG && (rhs & CF_CONST) == 0) {
   	/* We must promote the primary register to long */
   	g_reglong (rhs);
   	/* Get the new rhs type */
   	rhs = (rhs & ~CF_TYPE) | CF_LONG;
   	rtype = CF_LONG;
    } else if (ltype != CF_LONG && (lhs & CF_CONST) == 0 && rtype == CF_LONG) {
   	/* We must promote the lhs to long */
	if (lhs & CF_REG) {
	    g_reglong (lhs);
	} else {
   	    g_toslong (lhs);
	}
   	/* Get the new rhs type */
   	lhs = (lhs & ~CF_TYPE) | CF_LONG;
   	ltype = CF_LONG;
    }

    /* Determine the result type for the operation:
     *	- The result is const if both operands are const.
     *	- The result is unsigned if one of the operands is unsigned.
     *	- The result is long if one of the operands is long.
     *	- Otherwise the result is int sized.
     */
    result = (lhs & CF_CONST) & (rhs & CF_CONST);
    result |= (lhs & CF_UNSIGNED) | (rhs & CF_UNSIGNED);
    if (rtype == CF_LONG || ltype == CF_LONG) {
	result |= CF_LONG;
    } else {
	result |= CF_INT;
    }
    return result;
}



unsigned g_typecast (unsigned lhs, unsigned rhs)
/* Cast the value in the primary register to the operand size that is flagged
 * by the lhs value. Return the result value.
 */
{
    unsigned ltype, rtype;

    /* Get the type spec from the flags */
    ltype = lhs & CF_TYPE;
    rtype = rhs & CF_TYPE;

    /* Check if a conversion is needed */
    if (ltype == CF_LONG && rtype != CF_LONG && (rhs & CF_CONST) == 0) {
	/* We must promote the primary register to long */
	g_reglong (rhs);
    }

    /* Do not need any other action. If the left type is int, and the primary
     * register is long, it will be automagically truncated. If the right hand
     * side is const, it is not located in the primary register and handled by
     * the expression parser code.
     */

    /* Result is const if the right hand side was const */
    lhs |= (rhs & CF_CONST);

    /* The resulting type is that of the left hand side (that's why you called
     * this function :-)
     */
    return lhs;
}



void g_scale (unsigned flags, long val)
/* Scale the value in the primary register by the given value. If val is positive,
 * scale up, is val is negative, scale down. This function is used to scale
 * the operands or results of pointer arithmetic by the size of the type, the
 * pointer points to.
 */
{
    int p2;

    /* Value may not be zero */
    if (val == 0) {
       	Internal ("Data type has no size");
    } else if (val > 0) {

     	/* Scale up */
     	if ((p2 = powerof2 (val)) > 0 && p2 <= 3) {

     	    /* Factor is 2, 4 or 8, use special function */
     	    switch (flags & CF_TYPE) {

     		case CF_CHAR:
     		    if (flags & CF_FORCECHAR) {
     		     	while (p2--) {
     		     	    AddCodeLine ("asl a");
     	     	     	}
     	     	     	break;
     	     	    }
     	     	    /* FALLTHROUGH */

     	     	case CF_INT:
		    if (CodeSizeFactor >= (p2+1)*130U) {
     	     		AddCodeLine ("stx tmp1");
     	     	  	while (p2--) {
     	     		    AddCodeLine ("asl a");
	     		    AddCodeLine ("rol tmp1");
     	     		}
     	     		AddCodeLine ("ldx tmp1");
     	     	    } else {
     	     	       	if (flags & CF_UNSIGNED) {
     	     	     	    AddCodeLine ("jsr shlax%d", p2);
     	     	     	} else {
     	     	     	    AddCodeLine ("jsr aslax%d", p2);
     	     	     	}
     	     	    }
     	     	    break;

     	     	case CF_LONG:
     	     	    if (flags & CF_UNSIGNED) {
     	     	     	AddCodeLine ("jsr shleax%d", p2);
     	     	    } else {
     	     		AddCodeLine ("jsr asleax%d", p2);
     	     	    }
     	     	    break;

     		default:
     		    typeerror (flags);

     	    }

     	} else if (val != 1) {

       	    /* Use a multiplication instead */
     	    g_mul (flags | CF_CONST, val);

     	}

    } else {

     	/* Scale down */
     	val = -val;
     	if ((p2 = powerof2 (val)) > 0 && p2 <= 3) {

     	    /* Factor is 2, 4 or 8, use special function */
     	    switch (flags & CF_TYPE) {

     		case CF_CHAR:
     		    if (flags & CF_FORCECHAR) {
     			if (flags & CF_UNSIGNED) {
     			    while (p2--) {
     			      	AddCodeLine ("lsr a");
     			    }
     			    break;
     			} else if (p2 <= 2) {
     		  	    AddCodeLine ("cmp #$80");
     			    AddCodeLine ("ror a");
     			    break;
     			}
     		    }
     		    /* FALLTHROUGH */

     		case CF_INT:
     		    if (flags & CF_UNSIGNED) {
			if (CodeSizeFactor >= (p2+1)*130U) {
			    AddCodeLine ("stx tmp1");
			    while (p2--) {
	     		    	AddCodeLine ("lsr tmp1");
				AddCodeLine ("ror a");
			    }
			    AddCodeLine ("ldx tmp1");
			} else {
     			    AddCodeLine ("jsr lsrax%d", p2);
			}
     		    } else {
			if (CodeSizeFactor >= (p2+1)*150U) {
			    AddCodeLine ("stx tmp1");
			    while (p2--) {
			    	AddCodeLine ("cpx #$80");
    			    	AddCodeLine ("ror tmp1");
			    	AddCodeLine ("ror a");
			    }
			    AddCodeLine ("ldx tmp1");
			} else {
     			    AddCodeLine ("jsr asrax%d", p2);
	    	     	}
     		    }
     		    break;

     		case CF_LONG:
     		    if (flags & CF_UNSIGNED) {
     		     	AddCodeLine ("jsr lsreax%d", p2);
     		    } else {
     		       	AddCodeLine ("jsr asreax%d", p2);
     		    }
     		    break;

     		default:
     		    typeerror (flags);

     	    }

     	} else if (val != 1) {

       	    /* Use a division instead */
     	    g_div (flags | CF_CONST, val);

     	}
    }
}



/*****************************************************************************/
/*	     	Adds and subs of variables fix a fixed address		     */
/*****************************************************************************/



void g_addlocal (unsigned flags, int offs)
/* Add a local variable to ax */
{
    unsigned L;

    /* Correct the offset and check it */
    offs -= oursp;
    CheckLocalOffs (offs);

    switch (flags & CF_TYPE) {

     	case CF_CHAR:
	    L = GetLocalLabel();
	    AddCodeLine ("ldy #$%02X", offs & 0xFF);
	    AddCodeLine ("clc");
	    AddCodeLine ("adc (sp),y");
	    AddCodeLine ("bcc %s", LocalLabelName (L));
	    AddCodeLine ("inx");
	    g_defcodelabel (L);
	    break;

     	case CF_INT:
     	    AddCodeLine ("ldy #$%02X", offs & 0xFF);
     	    AddCodeLine ("clc");
     	    AddCodeLine ("adc (sp),y");
     	    AddCodeLine ("pha");
     	    AddCodeLine ("txa");
     	    AddCodeLine ("iny");
     	    AddCodeLine ("adc (sp),y");
     	    AddCodeLine ("tax");
     	    AddCodeLine ("pla");
     	    break;

     	case CF_LONG:
     	    /* Do it the old way */
       	    g_push (flags, 0);
     	    g_getlocal (flags, offs);
     	    g_add (flags, 0);
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_addstatic (unsigned flags, unsigned long label, unsigned offs)
/* Add a static variable to ax */
{
    unsigned L;

    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    L = GetLocalLabel();
	    AddCodeLine ("clc");
	    AddCodeLine ("adc %s", lbuf);
	    AddCodeLine ("bcc %s", LocalLabelName (L));
	    AddCodeLine ("inx");
	    g_defcodelabel (L);
	    break;

	case CF_INT:
	    AddCodeLine ("clc");
	    AddCodeLine ("adc %s", lbuf);
	    AddCodeLine ("tay");
	    AddCodeLine ("txa");
     	    AddCodeLine ("adc %s+1", lbuf);
	    AddCodeLine ("tax");
	    AddCodeLine ("tya");
	    break;

	case CF_LONG:
	    /* Do it the old way */
       	    g_push (flags, 0);
	    g_getstatic (flags, label, offs);
	    g_add (flags, 0);
	    break;

	default:
	    typeerror (flags);

    }
}



/*****************************************************************************/
/*   	    	       	     Special op= functions			     */
/*****************************************************************************/



void g_addeqstatic (unsigned flags, unsigned long label, unsigned offs,
       	    	    unsigned long val)
/* Emit += for a static variable */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
     	       	AddCodeLine ("ldx #$00");
       	    	if (flags & CF_CONST) {
     	    	    if (val == 1) {
     	    	   	AddCodeLine ("inc %s", lbuf);
     	     	   	AddCodeLine ("lda %s", lbuf);
     	     	    } else {
       	       	       	AddCodeLine ("lda #$%02X", (int)(val & 0xFF));
     	     	   	AddCodeLine ("clc");
     	     	   	AddCodeLine ("adc %s", lbuf);
     	     		AddCodeLine ("sta %s", lbuf);
     	     	    }
       	       	} else {
     	     	    AddCodeLine ("clc");
       	     	    AddCodeLine ("adc %s", lbuf);
     	     	    AddCodeLine ("sta %s", lbuf);
       	     	}
     	    	if ((flags & CF_UNSIGNED) == 0) {
		    unsigned L = GetLocalLabel();
     	    	    AddCodeLine ("bpl %s", LocalLabelName (L));
     		    AddCodeLine ("dex");
		    g_defcodelabel (L);
     		}
       		break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
       	    if (flags & CF_CONST) {
     		if (val == 1) {
     		    unsigned L = GetLocalLabel ();
     		    AddCodeLine ("inc %s", lbuf);
     		    AddCodeLine ("bne %s", LocalLabelName (L));
     		    AddCodeLine ("inc %s+1", lbuf);
     		    g_defcodelabel (L);
     		    AddCodeLine ("lda %s", lbuf);  		/* Hmmm... */
     		    AddCodeLine ("ldx %s+1", lbuf);
     		} else {
       	       	    AddCodeLine ("lda #$%02X", (int)(val & 0xFF));
     		    AddCodeLine ("clc");
     		    AddCodeLine ("adc %s", lbuf);
     		    AddCodeLine ("sta %s", lbuf);
     		    if (val < 0x100) {
     		       	unsigned L = GetLocalLabel ();
     		       	AddCodeLine ("bcc %s", LocalLabelName (L));
     		       	AddCodeLine ("inc %s+1", lbuf);
       		       	g_defcodelabel (L);
     		       	AddCodeLine ("ldx %s+1", lbuf);
     		    } else {
       	       	       	AddCodeLine ("lda #$%02X", (unsigned char)(val >> 8));
     		       	AddCodeLine ("adc %s+1", lbuf);
     		       	AddCodeLine ("sta %s+1", lbuf);
     		       	AddCodeLine ("tax");
     		       	AddCodeLine ("lda %s", lbuf);
     		    }
     		}
       	    } else {
     		AddCodeLine ("clc");
       		AddCodeLine ("adc %s", lbuf);
       		AddCodeLine ("sta %s", lbuf);
       		AddCodeLine ("txa");
       		AddCodeLine ("adc %s+1", lbuf);
       	     	AddCodeLine ("sta %s+1", lbuf);
       	     	AddCodeLine ("tax");
	     	AddCodeLine ("lda %s", lbuf);
	    }
       	    break;

       	case CF_LONG:
	    if (flags & CF_CONST) {
		if (val < 0x100) {
     		    AddCodeLine ("ldy #<(%s)", lbuf);
		    AddCodeLine ("sty ptr1");
		    AddCodeLine ("ldy #>(%s+1)", lbuf);
		    if (val == 1) {
			AddCodeLine ("jsr laddeq1");
		    } else {
			AddCodeLine ("lda #$%02X", (int)(val & 0xFF));
		     	AddCodeLine ("jsr laddeqa");
		    }
		} else {
		    g_getstatic (flags, label, offs);
		    g_inc (flags, val);
		    g_putstatic (flags, label, offs);
		}
	    } else {
		AddCodeLine ("ldy #<(%s)", lbuf);
		AddCodeLine ("sty ptr1");
		AddCodeLine ("ldy #>(%s+1)", lbuf);
		AddCodeLine ("jsr laddeq");
	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_addeqlocal (unsigned flags, int offs, unsigned long val)
/* Emit += for a local variable */
{
    /* Calculate the true offset, check it, load it into Y */
    offs -= oursp;
    CheckLocalOffs (offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
		ldyconst (offs);
		AddCodeLine ("ldx #$00");
		if (flags & CF_CONST) {
		    AddCodeLine ("clc");
		    AddCodeLine ("lda #$%02X", (int)(val & 0xFF));
		    AddCodeLine ("adc (sp),y");
		    AddCodeLine ("sta (sp),y");
		} else {
		    AddCodeLine ("clc");
		    AddCodeLine ("adc (sp),y");
		    AddCodeLine ("sta (sp),y");
		}
     	     	if ((flags & CF_UNSIGNED) == 0) {
		    unsigned L = GetLocalLabel();
     	     	    AddCodeLine ("bpl %s", LocalLabelName (L));
     	     	    AddCodeLine ("dex");
		    g_defcodelabel (L);
     	     	}
       	     	break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
     	    if (flags & CF_CONST) {
     	     	g_getimmed (flags, val, 0);
     	    }
     	    if (offs == 0) {
     	     	AddCodeLine ("jsr addeq0sp");
     	    } else {
     	     	ldyconst (offs);
     	     	AddCodeLine ("jsr addeqysp");
     	    }
       	    break;

       	case CF_LONG:
     	    if (flags & CF_CONST) {
	     	g_getimmed (flags, val, 0);
	    }
	    if (offs == 0) {
		AddCodeLine ("jsr laddeq0sp");
	    } else {
		ldyconst (offs);
		AddCodeLine ("jsr laddeqysp");
	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_addeqind (unsigned flags, unsigned offs, unsigned long val)
/* Emit += for the location with address in ax */
{
    /* If the offset is too large for a byte register, add the high byte
     * of the offset to the primary. Beware: We need a special correction
     * if the offset in the low byte will overflow in the operation.
     */
    offs = MakeByteOffs (flags, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
	    AddCodeLine ("sta ptr1");
	    AddCodeLine ("stx ptr1+1");
	    AddCodeLine ("ldy #$%02X", offs);
	    AddCodeLine ("ldx #$00");
	    AddCodeLine ("lda #$%02X", (int)(val & 0xFF));
	    AddCodeLine ("clc");
	    AddCodeLine ("adc (ptr1),y");
	    AddCodeLine ("sta (ptr1),y");
     	    break;

       	case CF_INT:
	    if (CodeSizeFactor >= 200) {
		/* Lots of code, use only if size is not important */
       	       	AddCodeLine ("sta ptr1");
		AddCodeLine ("stx ptr1+1");
		AddCodeLine ("ldy #$%02X", offs);
		AddCodeLine ("lda #$%02X", (int)(val & 0xFF));
		AddCodeLine ("clc");
		AddCodeLine ("adc (ptr1),y");
		AddCodeLine ("sta (ptr1),y");
		AddCodeLine ("pha");
		AddCodeLine ("iny");
		AddCodeLine ("lda #$%02X", (unsigned char)(val >> 8));
		AddCodeLine ("adc (ptr1),y");
		AddCodeLine ("sta (ptr1),y");
		AddCodeLine ("tax");
		AddCodeLine ("pla");
		break;
	    }
	    /* FALL THROUGH */

       	case CF_LONG:
       	    AddCodeLine ("jsr pushax");  	/* Push the address */
	    push (flags);		    	/* Correct the internal sp */
	    g_getind (flags, offs);		/* Fetch the value */
	    g_inc (flags, val);	   		/* Increment value in primary */
	    g_putind (flags, offs);		/* Store the value back */
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_subeqstatic (unsigned flags, unsigned long label, unsigned offs,
       		    unsigned long val)
/* Emit -= for a static variable */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
       		AddCodeLine ("ldx #$00");
       	  	if (flags & CF_CONST) {
       		    if (val == 1) {
       			AddCodeLine ("dec %s", lbuf);
       			AddCodeLine ("lda %s", lbuf);
       		    } else {
       		       	AddCodeLine ("sec");
       		     	AddCodeLine ("lda %s", lbuf);
       		     	AddCodeLine ("sbc #$%02X", (int)(val & 0xFF));
       		     	AddCodeLine ("sta %s", lbuf);
       		    }
       	  	} else {
       		    AddCodeLine ("sec");
       		    AddCodeLine ("sta tmp1");
       	  	    AddCodeLine ("lda %s", lbuf);
       	       	    AddCodeLine ("sbc tmp1");
       		    AddCodeLine ("sta %s", lbuf);
       	  	}
       		if ((flags & CF_UNSIGNED) == 0) {
		    unsigned L = GetLocalLabel();
       		    AddCodeLine ("bpl %s", LocalLabelName (L));
       		    AddCodeLine ("dex");
		    g_defcodelabel (L);
       	     	}
       	  	break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
	    AddCodeLine ("sec");
     	    if (flags & CF_CONST) {
	       	AddCodeLine ("lda %s", lbuf);
	  	AddCodeLine ("sbc #$%02X", (unsigned char)val);
	  	AddCodeLine ("sta %s", lbuf);
	   	if (val < 0x100) {
	  	    unsigned L = GetLocalLabel ();
	  	    AddCodeLine ("bcs %s", LocalLabelName (L));
		    AddCodeLine ("dec %s+1", lbuf);
		    g_defcodelabel (L);
		    AddCodeLine ("ldx %s+1", lbuf);
		} else {
		    AddCodeLine ("lda %s+1", lbuf);
		    AddCodeLine ("sbc #$%02X", (unsigned char)(val >> 8));
		    AddCodeLine ("sta %s+1", lbuf);
		    AddCodeLine ("tax");
		    AddCodeLine ("lda %s", lbuf);
		}
	    } else {
		AddCodeLine ("sta tmp1");
		AddCodeLine ("lda %s", lbuf);
	        AddCodeLine ("sbc tmp1");
		AddCodeLine ("sta %s", lbuf);
       	       	AddCodeLine ("stx tmp1");
		AddCodeLine ("lda %s+1", lbuf);
		AddCodeLine ("sbc tmp1");
		AddCodeLine ("sta %s+1", lbuf);
		AddCodeLine ("tax");
		AddCodeLine ("lda %s", lbuf);
	    }
       	    break;

       	case CF_LONG:
	    if (flags & CF_CONST) {
		if (val < 0x100) {
		    AddCodeLine ("ldy #<(%s)", lbuf);
		    AddCodeLine ("sty ptr1");
		    AddCodeLine ("ldy #>(%s+1)", lbuf);
		    if (val == 1) {
	     		AddCodeLine ("jsr lsubeq1");
		    } else {
			AddCodeLine ("lda #$%02X", (unsigned char)val);
			AddCodeLine ("jsr lsubeqa");
		    }
     		} else {
		    g_getstatic (flags, label, offs);
		    g_dec (flags, val);
		    g_putstatic (flags, label, offs);
		}
	    } else {
		AddCodeLine ("ldy #<(%s)", lbuf);
		AddCodeLine ("sty ptr1");
		AddCodeLine ("ldy #>(%s+1)", lbuf);
		AddCodeLine ("jsr lsubeq");
       	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_subeqlocal (unsigned flags, int offs, unsigned long val)
/* Emit -= for a local variable */
{
    /* Calculate the true offset, check it, load it into Y */
    offs -= oursp;
    CheckLocalOffs (offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
    	 	ldyconst (offs);
		AddCodeLine ("ldx #$00");
       	 	AddCodeLine ("sec");
		if (flags & CF_CONST) {
		    AddCodeLine ("lda (sp),y");
		    AddCodeLine ("sbc #$%02X", (unsigned char)val);
		} else {
		    AddCodeLine ("sta tmp1");
	     	    AddCodeLine ("lda (sp),y");
		    AddCodeLine ("sbc tmp1");
		}
       	 	AddCodeLine ("sta (sp),y");
		if ((flags & CF_UNSIGNED) == 0) {
		    unsigned L = GetLocalLabel();
	       	    AddCodeLine ("bpl %s", LocalLabelName (L));
		    AddCodeLine ("dex");
		    g_defcodelabel (L);
		}
       	 	break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
	    if (flags & CF_CONST) {
	     	g_getimmed (flags, val, 0);
	    }
	    if (offs == 0) {
	 	AddCodeLine ("jsr subeq0sp");
	    } else {
	 	ldyconst (offs);
	 	AddCodeLine ("jsr subeqysp");
	    }
       	    break;

       	case CF_LONG:
	    if (flags & CF_CONST) {
	     	g_getimmed (flags, val, 0);
	    }
	    if (offs == 0) {
		AddCodeLine ("jsr lsubeq0sp");
	    } else {
		ldyconst (offs);
		AddCodeLine ("jsr lsubeqysp");
	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_subeqind (unsigned flags, unsigned offs, unsigned long val)
/* Emit -= for the location with address in ax */
{
    /* If the offset is too large for a byte register, add the high byte
     * of the offset to the primary. Beware: We need a special correction
     * if the offset in the low byte will overflow in the operation.
     */
    offs = MakeByteOffs (flags, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
	    AddCodeLine ("sta ptr1");
	    AddCodeLine ("stx ptr1+1");
	    AddCodeLine ("ldy #$%02X", offs);
	    AddCodeLine ("ldx #$00");
	    AddCodeLine ("lda (ptr1),y");
	    AddCodeLine ("sec");
	    AddCodeLine ("sbc #$%02X", (unsigned char)val);
	    AddCodeLine ("sta (ptr1),y");
     	    break;

       	case CF_INT:
	    if (CodeSizeFactor >= 200) {
		/* Lots of code, use only if size is not important */
		AddCodeLine ("sta ptr1");
       	       	AddCodeLine ("stx ptr1+1");
		AddCodeLine ("ldy #$%02X", offs);
		AddCodeLine ("lda (ptr1),y");
		AddCodeLine ("sec");
		AddCodeLine ("sbc #$%02X", (unsigned char)val);
		AddCodeLine ("sta (ptr1),y");
		AddCodeLine ("pha");
		AddCodeLine ("iny");
		AddCodeLine ("lda (ptr1),y");
		AddCodeLine ("sbc #$%02X", (unsigned char)(val >> 8));
     		AddCodeLine ("sta (ptr1),y");
	     	AddCodeLine ("tax");
		AddCodeLine ("pla");
		break;
	    }
	    /* FALL THROUGH */

       	case CF_LONG:
       	    AddCodeLine ("jsr pushax");     	/* Push the address */
	    push (flags);  			/* Correct the internal sp */
	    g_getind (flags, offs);		/* Fetch the value */
	    g_dec (flags, val);			/* Increment value in primary */
	    g_putind (flags, offs);		/* Store the value back */
       	    break;

       	default:
       	    typeerror (flags);
    }
}



/*****************************************************************************/
/*		   Add a variable address to the value in ax		     */
/*****************************************************************************/



void g_addaddr_local (unsigned flags, int offs)
/* Add the address of a local variable to ax */
{
    unsigned L = 0;

    /* Add the offset */
    offs -= oursp;
    if (offs != 0) {
	/* We cannot address more then 256 bytes of locals anyway */
	L = GetLocalLabel();
	CheckLocalOffs (offs);
	AddCodeLine ("clc");
	AddCodeLine ("adc #$%02X", offs & 0xFF);
	/* Do also skip the CLC insn below */
       	AddCodeLine ("bcc %s", LocalLabelName (L));
	AddCodeLine ("inx");
    }

    /* Add the current stackpointer value */
    AddCodeLine ("clc");
    if (L != 0) {
	/* Label was used above */
	g_defcodelabel (L);
    }
    AddCodeLine ("adc sp");
    AddCodeLine ("tay");
    AddCodeLine ("txa");
    AddCodeLine ("adc sp+1");
    AddCodeLine ("tax");
    AddCodeLine ("tya");
}



void g_addaddr_static (unsigned flags, unsigned long label, unsigned offs)
/* Add the address of a static variable to ax */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Add the address to the current ax value */
    AddCodeLine ("clc");
    AddCodeLine ("adc #<(%s)", lbuf);
    AddCodeLine ("tay");
    AddCodeLine ("txa");
    AddCodeLine ("adc #>(%s)", lbuf);
    AddCodeLine ("tax");
    AddCodeLine ("tya");
}



/*****************************************************************************/
/*			  	     					     */
/*****************************************************************************/



void g_save (unsigned flags)
/* Copy primary register to hold register. */
{
    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

	case CF_CHAR:
     	    if (flags & CF_FORCECHAR) {
	     	AddCodeLine ("pha");
		break;
	    }
	    /* FALLTHROUGH */

	case CF_INT:
	    AddCodeLine ("sta regsave");
	    AddCodeLine ("stx regsave+1");
	    break;

	case CF_LONG:
	    AddCodeLine ("jsr saveeax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_restore (unsigned flags)
/* Copy hold register to primary. */
{
    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    if (flags & CF_FORCECHAR) {
	       	AddCodeLine ("pla");
	    	break;
	    }
	    /* FALLTHROUGH */

	case CF_INT:
	    AddCodeLine ("lda regsave");
	    AddCodeLine ("ldx regsave+1");
	    break;

	case CF_LONG:
	    AddCodeLine ("jsr resteax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_cmp (unsigned flags, unsigned long val)
/* Immidiate compare. The primary register will not be changed, Z flag
 * will be set.
 */
{
    unsigned L;

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

      	case CF_CHAR:
     	    if (flags & CF_FORCECHAR) {
	       	AddCodeLine ("cmp #$%02X", (unsigned char)val);
     	       	break;
     	    }
     	    /* FALLTHROUGH */

     	case CF_INT:
	    L = GetLocalLabel();
	    AddCodeLine ("cmp #$%02X", (unsigned char)val);
       	    AddCodeLine ("bne %s", LocalLabelName (L));
	    AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
	    g_defcodelabel (L);
     	    break;

        case CF_LONG:
	    Internal ("g_cmp: Long compares not implemented");
	    break;

	default:
	    typeerror (flags);
    }
}



static void oper (unsigned flags, unsigned long val, char** subs)
/* Encode a binary operation. subs is a pointer to four groups of three
 * strings:
 *   	0-2	--> Operate on ints
 *	3-5	--> Operate on unsigneds
 *	6-8	--> Operate on longs
 *	9-11	--> Operate on unsigned longs
 *
 * The first subroutine names in each string group is used to encode an
 * operation with a zero constant, the second to encode an operation with
 * a 8 bit constant, and the third is used in all other cases.
 */
{
    unsigned offs;

    /* Determine the offset into the array */
    offs = (flags & CF_UNSIGNED)? 3 : 0;
    switch (flags & CF_TYPE) {
 	case CF_CHAR:
 	case CF_INT:
 	    break;

 	case CF_LONG:
 	    offs += 6;
 	    break;

 	default:
 	    typeerror (flags);
    }

    /* Encode the operation */
    if (flags & CF_CONST) {
 	/* Constant value given */
 	if (val == 0 && subs [offs+0]) {
 	    /* Special case: constant with value zero */
 	    AddCodeLine ("jsr %s", subs [offs+0]);
 	} else if (val < 0x100 && subs [offs+1]) {
 	    /* Special case: constant with high byte zero */
 	    ldaconst (val);		/* Load low byte */
 	    AddCodeLine ("jsr %s", subs [offs+1]);
 	} else {
 	    /* Others: arbitrary constant value */
 	    g_getimmed (flags, val, 0);   	       	/* Load value */
 	    AddCodeLine ("jsr %s", subs [offs+2]);
 	}
    } else {
     	/* Value not constant (is already in (e)ax) */
 	AddCodeLine ("jsr %s", subs [offs+2]);
    }

    /* The operation will pop it's argument */
    pop (flags);
}



void g_test (unsigned flags)
/* Test the value in the primary and set the condition codes */
{
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
 	    if (flags & CF_FORCECHAR) {
 		AddCodeLine ("tax");
 		break;
 	    }
 	    /* FALLTHROUGH */

     	case CF_INT:
 	    AddCodeLine ("stx tmp1");
 	    AddCodeLine ("ora tmp1");
     	    break;

     	case CF_LONG:
     	    if (flags & CF_UNSIGNED) {
     	    	AddCodeLine ("jsr utsteax");
     	    } else {
     	    	AddCodeLine ("jsr tsteax");
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_push (unsigned flags, unsigned long val)
/* Push the primary register or a constant value onto the stack */
{
    unsigned char hi;

    if (flags & CF_CONST && (flags & CF_TYPE) != CF_LONG) {

     	/* We have a constant 8 or 16 bit value */
     	if ((flags & CF_TYPE) == CF_CHAR && (flags & CF_FORCECHAR)) {

     	    /* Handle as 8 bit value */
	    if (CodeSizeFactor >= 165 || val > 2) {
     	    	ldaconst (val);
     	    	AddCodeLine ("jsr pusha");
     	    } else {
     	    	AddCodeLine ("jsr pushc%d", (int) val);
     	    }

     	} else {

     	    /* Handle as 16 bit value */
     	    hi = (unsigned char) (val >> 8);
     	    if (val <= 7) {
		AddCodeLine ("jsr push%u", (unsigned) val);
     	    } else if (hi == 0 || hi == 0xFF) {
     	    	/* Use special function */
     	    	ldaconst (val);
       	       	AddCodeLine ("jsr %s", (hi == 0)? "pusha0" : "pushaFF");
     	    } else {
     	    	/* Long way ... */
     	    	g_getimmed (flags, val, 0);
     	    	AddCodeLine ("jsr pushax");
     	    }
     	}

    } else {

     	/* Value is not 16 bit or not constant */
     	if (flags & CF_CONST) {
     	    /* Constant 32 bit value, load into eax */
     	    g_getimmed (flags, val, 0);
     	}

     	/* Push the primary register */
     	switch (flags & CF_TYPE) {

     	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    /* Handle as char */
     		    AddCodeLine ("jsr pusha");
     		    break;
     		}
     		/* FALL THROUGH */
     	    case CF_INT:
     		AddCodeLine ("jsr pushax");
     		break;

     	    case CF_LONG:
     	     	AddCodeLine ("jsr pusheax");
     		break;

     	    default:
     		typeerror (flags);

     	}

    }

    /* Adjust the stack offset */
    push (flags);
}



void g_swap (unsigned flags)
/* Swap the primary register and the top of the stack. flags give the type
 * of *both* values (must have same size).
 */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    AddCodeLine ("jsr swapstk");
	    break;

	case CF_LONG:
	    AddCodeLine ("jsr swapestk");
	    break;

	default:
	    typeerror (flags);

    }
}



void g_call (unsigned Flags, const char* Label, unsigned ArgSize)
/* Call the specified subroutine name */
{
    if ((Flags & CF_FIXARGC) == 0) {
	/* Pass the argument count */
	ldyconst (ArgSize);
    }
    AddCodeLine ("jsr _%s", Label);
    oursp += ArgSize;		/* callee pops args */
}



void g_callind (unsigned Flags, unsigned ArgSize)
/* Call subroutine with address in AX */
{
    if ((Flags & CF_FIXARGC) == 0) {
	/* Pass arg count */
	ldyconst (ArgSize);
    }
    AddCodeLine ("jsr callax");	/* do the call */
    oursp += ArgSize;			/* callee pops args */
}



void g_jump (unsigned Label)
/* Jump to specified internal label number */
{
    AddCodeLine ("jmp %s", LocalLabelName (Label));
}



void g_switch (unsigned Flags)
/* Output switch statement preamble */
{
    switch (Flags & CF_TYPE) {

     	case CF_CHAR:
     	case CF_INT:
     	    AddCodeLine ("jsr switch");
     	    break;

     	case CF_LONG:
     	    AddCodeLine ("jsr lswitch");
     	    break;

     	default:
     	    typeerror (Flags);

    }
}



void g_case (unsigned flags, unsigned label, unsigned long val)
/* Create table code for one case selector */
{
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
    	case CF_INT:
    	    AddCodeLine (".word $%04X, %s",
			 (unsigned)(val & 0xFFFF),
			 LocalLabelName (label));
       	    break;

    	case CF_LONG:
	    AddCodeLine (".dword $%08lX", val);
	    AddCodeLine (".word %s", LocalLabelName (label));
    	    break;

    	default:
    	    typeerror (flags);

    }
}



void g_truejump (unsigned flags, unsigned label)
/* Jump to label if zero flag clear */
{
    AddCodeLine ("jne %s", LocalLabelName (label));
}



void g_falsejump (unsigned flags, unsigned label)
/* Jump to label if zero flag set */
{
    AddCodeLine ("jeq %s", LocalLabelName (label));
}



static void mod_internal (int k, char* verb1, char* verb2)
{
    if (k <= 8) {
	AddCodeLine ("jsr %ssp%c", verb1, k + '0');
    } else {
	CheckLocalOffs (k);
	ldyconst (k);
	AddCodeLine ("jsr %ssp", verb2);
    }
}



void g_space (int space)
/* Create or drop space on the stack */
{
    if (space < 0) {
     	mod_internal (-space, "inc", "addy");
    } else if (space > 0) {
     	mod_internal (space, "dec", "suby");
    }
}



void g_cstackcheck (void)
/* Check for a C stack overflow */
{
    AddCodeLine ("jsr cstkchk");
}



void g_stackcheck (void)
/* Check for a stack overflow */
{
    AddCodeLine ("jsr stkchk");
}



void g_add (unsigned flags, unsigned long val)
/* Primary = TOS + Primary */
{
    static char* ops [12] = {
     	0,		"tosadda0",	"tosaddax",
     	0,		"tosadda0",	"tosaddax",
     	0,		0,	 	"tosaddeax",
     	0,		0,	 	"tosaddeax",
    };

    if (flags & CF_CONST) {
    	flags &= ~CF_FORCECHAR;	/* Handle chars as ints */
     	g_push (flags & ~CF_CONST, 0);
    }
    oper (flags, val, ops);
}



void g_sub (unsigned flags, unsigned long val)
/* Primary = TOS - Primary */
{
    static char* ops [12] = {
     	0,		"tossuba0",	"tossubax",
     	0,		"tossuba0",	"tossubax",
     	0,		0,	 	"tossubeax",
     	0,		0,	 	"tossubeax",
    };

    if (flags & CF_CONST) {
    	flags &= ~CF_FORCECHAR;	/* Handle chars as ints */
     	g_push (flags & ~CF_CONST, 0);
    }
    oper (flags, val, ops);
}



void g_rsub (unsigned flags, unsigned long val)
/* Primary = Primary - TOS */
{
    static char* ops [12] = {
	0,		"tosrsuba0",	"tosrsubax",
	0,		"tosrsuba0",	"tosrsubax",
	0,		0,	 	"tosrsubeax",
	0,		0,	 	"tosrsubeax",
    };
    oper (flags, val, ops);
}



void g_mul (unsigned flags, unsigned long val)
/* Primary = TOS * Primary */
{
    static char* ops [12] = {
     	0,		"tosmula0",	"tosmulax",
     	0,   		"tosumula0",	"tosumulax",
     	0,		0,	 	"tosmuleax",
     	0,		0,	 	"tosumuleax",
    };

    int p2;

    /* Do strength reduction if the value is constant and a power of two */
    if (flags & CF_CONST && (p2 = powerof2 (val)) >= 0) {
     	/* Generate a shift instead */
     	g_asl (flags, p2);
	return;
    }

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      		if (flags & CF_FORCECHAR) {
		    /* Handle some special cases */
		    switch (val) {

		     	case 3:
		     	    AddCodeLine ("sta tmp1");
		     	    AddCodeLine ("asl a");
		     	    AddCodeLine ("clc");
		     	    AddCodeLine ("adc tmp1");
		     	    return;

		     	case 5:
		     	    AddCodeLine ("sta tmp1");
		     	    AddCodeLine ("asl a");
		     	    AddCodeLine ("asl a");
		     	    AddCodeLine ("clc");
     		     	    AddCodeLine ("adc tmp1");
		     	    return;

		     	case 10:
		     	    AddCodeLine ("sta tmp1");
		     	    AddCodeLine ("asl a");
		     	    AddCodeLine ("asl a");
	     	     	    AddCodeLine ("clc");
		     	    AddCodeLine ("adc tmp1");
		     	    AddCodeLine ("asl a");
		     	    return;
		    }
      		}
     		/* FALLTHROUGH */

	    case CF_INT:
		break;

	    case CF_LONG:
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
    	flags &= ~CF_FORCECHAR;	/* Handle chars as ints */
     	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_div (unsigned flags, unsigned long val)
/* Primary = TOS / Primary */
{
    static char* ops [12] = {
     	0,		"tosdiva0",	"tosdivax",
     	0,		"tosudiva0",	"tosudivax",
	0,		0,  		"tosdiveax",
	0,		0,  		"tosudiveax",
    };

    /* Do strength reduction if the value is constant and a power of two */
    int p2;
    if ((flags & CF_CONST) && (p2 = powerof2 (val)) >= 0) {
	/* Generate a shift instead */
	g_asr (flags, p2);
    } else {
	/* Generate a division */
	if (flags & CF_CONST) {
	    /* lhs is not on stack */
    	    flags &= ~CF_FORCECHAR;	/* Handle chars as ints */
	    g_push (flags & ~CF_CONST, 0);
     	}
	oper (flags, val, ops);
    }
}



void g_mod (unsigned flags, unsigned long val)
/* Primary = TOS % Primary */
{
    static char* ops [12] = {
     	0,		"tosmoda0",	"tosmodax",
     	0,		"tosumoda0",	"tosumodax",
     	0,		0,  		"tosmodeax",
     	0,		0,  		"tosumodeax",
    };
    int p2;

    /* Check if we can do some cost reduction */
    if ((flags & CF_CONST) && (flags & CF_UNSIGNED) && val != 0xFFFFFFFF && (p2 = powerof2 (val)) >= 0) {
     	/* We can do that with an AND operation */
     	g_and (flags, val - 1);
    } else {
      	/* Do it the hard way... */
     	if (flags & CF_CONST) {
     	    /* lhs is not on stack */
    	    flags &= ~CF_FORCECHAR;	/* Handle chars as ints */
     	    g_push (flags & ~CF_CONST, 0);
     	}
      	oper (flags, val, ops);
    }
}



void g_or (unsigned flags, unsigned long val)
/* Primary = TOS | Primary */
{
    static char* ops [12] = {
      	0,  	     	"tosora0",	"tosorax",
      	0,  	     	"tosora0",	"tosorax",
      	0,  	     	0,  		"tosoreax",
      	0,  	     	0,     		"tosoreax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      		if (flags & CF_FORCECHAR) {
     		    if ((val & 0xFF) != 0xFF) {
       	       	        AddCodeLine ("ora #$%02X", (unsigned char)val);
     		    }
      		    return;
      		}
     		/* FALLTHROUGH */

	    case CF_INT:
		if (val <= 0xFF) {
		    AddCodeLine ("ora #$%02X", (unsigned char)val);
		    return;
     		}
		break;

	    case CF_LONG:
		if (val <= 0xFF) {
		    AddCodeLine ("ora #$%02X", (unsigned char)val);
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_xor (unsigned flags, unsigned long val)
/* Primary = TOS ^ Primary */
{
    static char* ops [12] = {
	0,		"tosxora0",	"tosxorax",
	0,		"tosxora0",	"tosxorax",
	0,		0,	   	"tosxoreax",
	0,		0,	   	"tosxoreax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      		if (flags & CF_FORCECHAR) {
     		    if ((val & 0xFF) != 0) {
       	       	    	AddCodeLine ("eor #$%02X", (unsigned char)val);
     		    }
      		    return;
      		}
     		/* FALLTHROUGH */

	    case CF_INT:
		if (val <= 0xFF) {
		    if (val != 0) {
		     	AddCodeLine ("eor #$%02X", (unsigned char)val);
		    }
		    return;
		} else if ((val & 0xFF) == 0) {
		    AddCodeLine ("pha");
	     	    AddCodeLine ("txa");
		    AddCodeLine ("eor #$%02X", (unsigned char)(val >> 8));
		    AddCodeLine ("tax");
		    AddCodeLine ("pla");
		    return;
		}
		break;

	    case CF_LONG:
		if (val <= 0xFF) {
		    if (val != 0) {
       	       	       	AddCodeLine ("eor #$%02X", (unsigned char)val);
		    }
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_and (unsigned flags, unsigned long val)
/* Primary = TOS & Primary */
{
    static char* ops [12] = {
     	0,	     	"tosanda0",	"tosandax",
     	0,	     	"tosanda0",	"tosandax",
      	0,	     	0,		"tosandeax",
     	0,	     	0,		"tosandeax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	switch (flags & CF_TYPE) {

     	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    AddCodeLine ("and #$%02X", (unsigned char)val);
     		    return;
     		}
     		/* FALLTHROUGH */
     	    case CF_INT:
		if ((val & 0xFFFF) != 0xFFFF) {
       	       	    if (val <= 0xFF) {
		    	ldxconst (0);
		    	if (val == 0) {
		    	    ldaconst (0);
		    	} else if (val != 0xFF) {
		       	    AddCodeLine ("and #$%02X", (unsigned char)val);
		    	}
		    } else if ((val & 0xFF00) == 0xFF00) {
		    	AddCodeLine ("and #$%02X", (unsigned char)val);
		    } else if ((val & 0x00FF) == 0x0000) {
			AddCodeLine ("txa");
			AddCodeLine ("and #$%02X", (unsigned char)(val >> 8));
			AddCodeLine ("tax");
			ldaconst (0);
		    } else {
			AddCodeLine ("tay");
			AddCodeLine ("txa");
			AddCodeLine ("and #$%02X", (unsigned char)(val >> 8));
			AddCodeLine ("tax");
			AddCodeLine ("tya");
			if ((val & 0x00FF) != 0x00FF) {
			    AddCodeLine ("and #$%02X", (unsigned char)val);
			}
		    }
		}
		return;

	    case CF_LONG:
		if (val <= 0xFF) {
		    ldxconst (0);
		    AddCodeLine ("stx sreg+1");
	     	    AddCodeLine ("stx sreg");
		    if ((val & 0xFF) != 0xFF) {
		     	 AddCodeLine ("and #$%02X", (unsigned char)val);
		    }
		    return;
		} else if (val == 0xFF00) {
		    ldaconst (0);
		    AddCodeLine ("sta sreg+1");
		    AddCodeLine ("sta sreg");
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_asr (unsigned flags, unsigned long val)
/* Primary = TOS >> Primary */
{
    static char* ops [12] = {
      	0,	     	"tosasra0",	"tosasrax",
      	0,	     	"tosshra0",	"tosshrax",
      	0,	     	0,		"tosasreax",
      	0,	     	0,		"tosshreax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      	    case CF_INT:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("jsr shrax%ld", val);
		    } else {
		       	AddCodeLine ("jsr asrax%ld", val);
		    }
		    return;
		} else if (val == 8 && (flags & CF_UNSIGNED)) {
      		    AddCodeLine ("txa");
      		    ldxconst (0);
		    return;
		}
		break;

	    case CF_LONG:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("jsr shreax%ld", val);
		    } else {
		       	AddCodeLine ("jsr asreax%ld", val);
		    }
		    return;
		} else if (val == 8 && (flags & CF_UNSIGNED)) {
		    AddCodeLine ("txa");
		    AddCodeLine ("ldx sreg");
		    AddCodeLine ("ldy sreg+1");
		    AddCodeLine ("sty sreg");
		    AddCodeLine ("ldy #$00");
     		    AddCodeLine ("sty sreg+1");
		    return;
     		} else if (val == 16) {
		    AddCodeLine ("ldy #$00");
		    AddCodeLine ("ldx sreg+1");
		    if ((flags & CF_UNSIGNED) == 0) {
			unsigned L = GetLocalLabel();
		        AddCodeLine ("bpl %s", LocalLabelName (L));
		        AddCodeLine ("dey");
			g_defcodelabel (L);
		    }
     		    AddCodeLine ("lda sreg");
		    AddCodeLine ("sty sreg+1");
		    AddCodeLine ("sty sreg");
	     	    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_asl (unsigned flags, unsigned long val)
/* Primary = TOS << Primary */
{
    static char* ops [12] = {
	0,	     	"tosasla0",    	"tosaslax",
	0,	     	"tosshla0",    	"tosshlax",
	0,	     	0,     	       	"tosasleax",
	0,	     	0,     	       	"tosshleax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      	    case CF_INT:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("jsr shlax%ld", val);
		    } else {
	     	    	AddCodeLine ("jsr aslax%ld", val);
		    }
		    return;
      		} else if (val == 8) {
      		    AddCodeLine ("tax");
      		    AddCodeLine ("lda #$00");
     		    return;
     		}
     		break;

	    case CF_LONG:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("jsr shleax%ld", val);
		    } else {
		       	AddCodeLine ("jsr asleax%ld", val);
		    }
		    return;
		} else if (val == 8) {
		    AddCodeLine ("ldy sreg");
		    AddCodeLine ("sty sreg+1");
		    AddCodeLine ("stx sreg");
		    AddCodeLine ("tax");
		    AddCodeLine ("lda #$00");
		    return;
		} else if (val == 16) {
		    AddCodeLine ("stx sreg+1");
		    AddCodeLine ("sta sreg");
		    AddCodeLine ("lda #$00");
		    AddCodeLine ("tax");
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_neg (unsigned flags)
/* Primary = -Primary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
     	case CF_INT:
	    AddCodeLine ("jsr negax");
	    break;

	case CF_LONG:
	    AddCodeLine ("jsr negeax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_bneg (unsigned flags)
/* Primary = !Primary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    AddCodeLine ("jsr bnega");
	    break;

	case CF_INT:
	    AddCodeLine ("jsr bnegax");
	    break;

	case CF_LONG:
     	    AddCodeLine ("jsr bnegeax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_com (unsigned flags)
/* Primary = ~Primary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    AddCodeLine ("jsr complax");
	    break;

	case CF_LONG:
	    AddCodeLine ("jsr compleax");
     	    break;

	default:
     	    typeerror (flags);
    }
}



void g_inc (unsigned flags, unsigned long val)
/* Increment the primary register by a given number */
{
    /* Don't inc by zero */
    if (val == 0) {
     	return;
    }

    /* Generate code for the supported types */
    flags &= ~CF_CONST;
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
     	    if (flags & CF_FORCECHAR) {
		if (CPU == CPU_65C02 && val <= 2) {
		    while (val--) {
		 	AddCodeLine ("ina");
		    }
	     	} else {
		    AddCodeLine ("clc");
		    AddCodeLine ("adc #$%02X", (unsigned char)val);
		}
     		break;
     	    }
     	    /* FALLTHROUGH */

     	case CF_INT:
	    if (CPU == CPU_65C02 && val == 1) {
		unsigned L = GetLocalLabel();
		AddCodeLine ("ina");
		AddCodeLine ("bne %s", LocalLabelName (L));
		AddCodeLine ("inx");
		g_defcodelabel (L);
     	    } else if (CodeSizeFactor < 200) {
     		/* Use jsr calls */
     		if (val <= 8) {
     		    AddCodeLine ("jsr incax%lu", val);
     		} else if (val <= 255) {
     		    ldyconst (val);
     		    AddCodeLine ("jsr incaxy");
     		} else {
     		    g_add (flags | CF_CONST, val);
     		}
     	    } else {
     		/* Inline the code */
		if (val < 0x300) {
		    if ((val & 0xFF) != 0) {
			unsigned L = GetLocalLabel();
		       	AddCodeLine ("clc");
		       	AddCodeLine ("adc #$%02X", (unsigned char) val);
		       	AddCodeLine ("bcc %s", LocalLabelName (L));
		       	AddCodeLine ("inx");
			g_defcodelabel (L);
		    }
     		    if (val >= 0x100) {
     		       	AddCodeLine ("inx");
     		    }
     		    if (val >= 0x200) {
     		       	AddCodeLine ("inx");
     		    }
     		} else {
		    AddCodeLine ("clc");
		    if ((val & 0xFF) != 0) {
	     	       	AddCodeLine ("adc #$%02X", (unsigned char) val);
		    }
     		    AddCodeLine ("pha");
     		    AddCodeLine ("txa");
     		    AddCodeLine ("adc #$%02X", (unsigned char) (val >> 8));
     		    AddCodeLine ("tax");
     		    AddCodeLine ("pla");
     		}
     	    }
     	    break;

       	case CF_LONG:
     	    if (val <= 255) {
     		ldyconst (val);
     		AddCodeLine ("jsr inceaxy");
     	    } else {
     		g_add (flags | CF_CONST, val);
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_dec (unsigned flags, unsigned long val)
/* Decrement the primary register by a given number */
{
    /* Don't dec by zero */
    if (val == 0) {
     	return;
    }

    /* Generate code for the supported types */
    flags &= ~CF_CONST;
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
	    if (flags & CF_FORCECHAR) {
		if (CPU == CPU_65C02 && val <= 2) {
		    while (val--) {
		 	AddCodeLine ("dea");
		    }
		} else {
		    AddCodeLine ("sec");
	     	    AddCodeLine ("sbc #$%02X", (unsigned char)val);
		}
		break;
     	    }
	    /* FALLTHROUGH */

     	case CF_INT:
	    if (CodeSizeFactor < 200) {
		/* Use subroutines */
		if (val <= 8) {
		    AddCodeLine ("jsr decax%d", (int) val);
		} else if (val <= 255) {
		    ldyconst (val);
		    AddCodeLine ("jsr decaxy");
		} else {
		    g_sub (flags | CF_CONST, val);
		}
	    } else {
		/* Inline the code */
		if (val < 0x300) {
		    if ((val & 0xFF) != 0) {
			unsigned L = GetLocalLabel();
		       	AddCodeLine ("sec");
		       	AddCodeLine ("sbc #$%02X", (unsigned char) val);
     		       	AddCodeLine ("bcs %s", LocalLabelName (L));
     		       	AddCodeLine ("dex");
			g_defcodelabel (L);
		    }
     		    if (val >= 0x100) {
     		       	AddCodeLine ("dex");
     		    }
     		    if (val >= 0x200) {
     		       	AddCodeLine ("dex");
     		    }
     		} else {
		    AddCodeLine ("sec");
		    if ((val & 0xFF) != 0) {
	     	       	AddCodeLine ("sbc #$%02X", (unsigned char) val);
		    }
     		    AddCodeLine ("pha");
     		    AddCodeLine ("txa");
     		    AddCodeLine ("sbc #$%02X", (unsigned char) (val >> 8));
     		    AddCodeLine ("tax");
     		    AddCodeLine ("pla");
     		}
	    }
     	    break;

     	case CF_LONG:
     	    if (val <= 255) {
     		ldyconst (val);
     		AddCodeLine ("jsr deceaxy");
     	    } else {
     		g_sub (flags | CF_CONST, val);
	    }
	    break;

	default:
	    typeerror (flags);

    }
}



/*
 * Following are the conditional operators. They compare the TOS against
 * the primary and put a literal 1 in the primary if the condition is
 * true, otherwise they clear the primary register
 */



void g_eq (unsigned flags, unsigned long val)
/* Test for equal */
{
    static char* ops [12] = {
     	"toseq00",	"toseqa0",	"toseqax",
     	"toseq00",	"toseqa0",	"toseqax",
     	0,		0,	  	"toseqeax",
     	0,		0,	  	"toseqeax",
    };

    unsigned L;

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
		    AddCodeLine ("jsr booleq");
		    return;
		}
     		/* FALLTHROUGH */

      	    case CF_INT:
		L = GetLocalLabel();
     		AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
       	       	AddCodeLine ("bne %s", LocalLabelName (L));
     		AddCodeLine ("cmp #$%02X", (unsigned char)val);
		g_defcodelabel (L);
     		AddCodeLine ("jsr booleq");
     		return;

     	    case CF_LONG:
     		break;

     	    default:
     		typeerror (flags);
     	}

     	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
     	 */
     	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_ne (unsigned flags, unsigned long val)
/* Test for not equal */
{
    static char* ops [12] = {
     	"tosne00",	"tosnea0",	"tosneax",
     	"tosne00",	"tosnea0",	"tosneax",
     	0,		0,		"tosneeax",
     	0,		0,		"tosneeax",
    };

    unsigned L;

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
     		    AddCodeLine ("jsr boolne");
     		    return;
     		}
     		/* FALLTHROUGH */

      	    case CF_INT:
		L = GetLocalLabel();
     		AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
     		AddCodeLine ("bne %s", LocalLabelName (L));
     		AddCodeLine ("cmp #$%02X", (unsigned char)val);
		g_defcodelabel (L);
     		AddCodeLine ("jsr boolne");
     		return;

     	    case CF_LONG:
     		break;

     	    default:
     		typeerror (flags);
     	}

     	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
     	 */
     	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_lt (unsigned flags, unsigned long val)
/* Test for less than */
{
    static char* ops [12] = {
     	"toslt00",	"toslta0",    	"tosltax",
     	"tosult00",	"tosulta0",   	"tosultax",
     	0,		0,    	      	"toslteax",
     	0,		0,    	      	"tosulteax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	/* Give a warning in some special cases */
     	if ((flags & CF_UNSIGNED) && val == 0) {
     	    Warning ("Condition is never true");
     	}

     	/* Look at the type */
     	switch (flags & CF_TYPE) {

     	    case CF_CHAR:
     	       	if (flags & CF_FORCECHAR) {
     	       	    AddCodeLine ("cmp #$%02X", (unsigned char)val);
     	       	    if (flags & CF_UNSIGNED) {
     	       		AddCodeLine ("jsr boolult");
     	       	    } else {
     	       	        AddCodeLine ("jsr boollt");
     	       	    }
     	       	    return;
     	       	}
     	       	/* FALLTHROUGH */

     	    case CF_INT:
	       	if ((flags & CF_UNSIGNED) == 0 && val == 0) {
	       	    /* If we have a signed compare against zero, we only need to
	       	     * test the high byte.
	       	     */
	       	    AddCodeLine ("txa");
	       	    AddCodeLine ("jsr boollt");
	       	    return;
	       	}
	       	/* Direct code only for unsigned data types */
	       	if (flags & CF_UNSIGNED) {
		    unsigned L = GetLocalLabel();
	       	    AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
       	       	    AddCodeLine ("bne %s", LocalLabelName (L));
     	       	    AddCodeLine ("cmp #$%02X", (unsigned char)val);
		    g_defcodelabel (L);
	 	    AddCodeLine ("jsr boolult");
	 	    return;
     	 	}
     	 	break;

     	    case CF_LONG:
		if ((flags & CF_UNSIGNED) == 0 && val == 0) {
		    /* If we have a signed compare against zero, we only need to
		     * test the high byte.
		     */
		    AddCodeLine ("lda sreg+1");
		    AddCodeLine ("jsr boollt");
		    return;
		}
     	 	break;

     	    default:
	 	typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_le (unsigned flags, unsigned long val)
/* Test for less than or equal to */
{
    static char* ops [12] = {
	"tosle00",   	"toslea0",	"tosleax",
	"tosule00",  	"tosulea0",	"tosuleax",
	0,	     	0,    		"tosleeax",
	0,	     	0,    		"tosuleeax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	/* Look at the type */
     	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
		    if (flags & CF_UNSIGNED) {
		     	AddCodeLine ("jsr boolule");
		    } else {
		        AddCodeLine ("jsr boolle");
		    }
		    return;
		}
		/* FALLTHROUGH */

	    case CF_INT:
		if (flags & CF_UNSIGNED) {
		    unsigned L = GetLocalLabel();
		    AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
       	       	    AddCodeLine ("bne %s", LocalLabelName (L));
     		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
	            g_defcodelabel (L);
		    AddCodeLine ("jsr boolule");
		    return;
		}
	  	break;

	    case CF_LONG:
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_gt (unsigned flags, unsigned long val)
/* Test for greater than */
{
    static char* ops [12] = {
	"tosgt00",    	"tosgta0",	"tosgtax",
	"tosugt00",   	"tosugta0",	"tosugtax",
	0,	      	0,	    	"tosgteax",
	0,	      	0, 	    	"tosugteax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	/* Look at the type */
     	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
		    if (flags & CF_UNSIGNED) {
		      	/* If we have a compare > 0, we will replace it by
		      	 * != 0 here, since both are identical but the latter
		      	 * is easier to optimize.
		      	 */
		      	if (val & 0xFF) {
		       	    AddCodeLine ("jsr boolugt");
		      	} else {
		      	    AddCodeLine ("jsr boolne");
		      	}
		    } else {
	     	        AddCodeLine ("jsr boolgt");
		    }
		    return;
		}
		/* FALLTHROUGH */

	    case CF_INT:
		if (flags & CF_UNSIGNED) {
		    /* If we have a compare > 0, we will replace it by
		     * != 0 here, since both are identical but the latter
	 	     * is easier to optimize.
	 	     */
	 	    if ((val & 0xFFFF) == 0) {
	 		AddCodeLine ("stx tmp1");
	 		AddCodeLine ("ora tmp1");
	 		AddCodeLine ("jsr boolne");
	 	    } else {
			unsigned L = GetLocalLabel();
       	       	       	AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
	 		AddCodeLine ("bne %s", LocalLabelName (L));
	 		AddCodeLine ("cmp #$%02X", (unsigned char)val);
			g_defcodelabel (L);
       	       	       	AddCodeLine ("jsr boolugt");
	 	    }
	 	    return;
       	       	}
	 	break;

	    case CF_LONG:
	 	break;

	    default:
	 	typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_ge (unsigned flags, unsigned long val)
/* Test for greater than or equal to */
{
    static char* ops [12] = {
     	"tosge00",	"tosgea0",  	"tosgeax",
     	"tosuge00",	"tosugea0",	"tosugeax",
     	0,		0,		"tosgeeax",
     	0,		0,		"tosugeeax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

	/* Give a warning in some special cases */
	if ((flags & CF_UNSIGNED) && val == 0) {
     	    Warning ("Condition is always true");
	}

	/* Look at the type */
	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
		    if (flags & CF_UNSIGNED) {
			AddCodeLine ("jsr booluge");
		    } else {
		        AddCodeLine ("jsr boolge");
		    }
		    return;
		}
		/* FALLTHROUGH */

	    case CF_INT:
		if ((flags & CF_UNSIGNED) == 0 && val == 0) {
		    /* If we have a signed compare against zero, we only need to
		     * test the high byte.
		     */
		    AddCodeLine ("txa");
		    AddCodeLine ("jsr boolge");
		    return;
		}
		/* Direct code only for unsigned data types */
		if (flags & CF_UNSIGNED) {
		    unsigned L = GetLocalLabel();
       	       	    AddCodeLine ("cpx #$%02X", (unsigned char)(val >> 8));
       	       	    AddCodeLine ("bne %s", LocalLabelName (L));
     		    AddCodeLine ("cmp #$%02X", (unsigned char)val);
		    g_defcodelabel (L);
		    AddCodeLine ("jsr booluge");
		    return;
	 	}
	     	break;

	    case CF_LONG:
		if ((flags & CF_UNSIGNED) == 0 && val == 0) {
		    /* If we have a signed compare against zero, we only need to
		     * test the high byte.
		     */
		    AddCodeLine ("lda sreg+1");
		    AddCodeLine ("jsr boolge");
		    return;
		}
	 	break;

	    default:
	 	typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



/*****************************************************************************/
/*   			   Allocating static storage	     	 	     */
/*****************************************************************************/



void g_res (unsigned n)
/* Reserve static storage, n bytes */
{
    AddDataLine ("\t.res\t%u,$00", n);
}



void g_defdata (unsigned flags, unsigned long val, unsigned offs)
/* Define data with the size given in flags */
{
    if (flags & CF_CONST) {

	/* Numeric constant */
	switch (flags & CF_TYPE) {

	    case CF_CHAR:
	     	AddDataLine ("\t.byte\t$%02lX", val & 0xFF);
		break;

	    case CF_INT:
		AddDataLine ("\t.word\t$%04lX", val & 0xFFFF);
		break;

	    case CF_LONG:
		AddDataLine ("\t.dword\t$%08lX", val & 0xFFFFFFFF);
		break;

	    default:
		typeerror (flags);
		break;

	}

    } else {

	/* Create the correct label name */
	const char* Label = GetLabelName (flags, val, offs);

	/* Labels are always 16 bit */
	AddDataLine ("\t.word\t%s", Label);

    }
}



void g_defbytes (const void* Bytes, unsigned Count)
/* Output a row of bytes as a constant */
{
    unsigned Chunk;
    char Buf [128];
    char* B;

    /* Cast the buffer pointer */
    const unsigned char* Data = (const unsigned char*) Bytes;

    /* Output the stuff */
    while (Count) {

     	/* How many go into this line? */
     	if ((Chunk = Count) > 16) {
     	    Chunk = 16;
     	}
     	Count -= Chunk;

     	/* Output one line */
	strcpy (Buf, "\t.byte\t");
       	B = Buf + 7;
     	do {
	    B += sprintf (B, "$%02X", *Data++);
     	    if (--Chunk) {
		*B++ = ',';
     	    }
     	} while (Chunk);

	/* Output the line */
       	AddDataLine (Buf);
    }
}



void g_zerobytes (unsigned n)
/* Output n bytes of data initialized with zero */
{
    AddDataLine ("\t.res\t%u,$00", n);
}



/*****************************************************************************/
/*			 User supplied assembler code			     */
/*****************************************************************************/



void g_asmcode (const char* Line, int Len)
/* Output one line of assembler code. If Len is greater than zero, it is used
 * as the maximum number of characters to use from Line.
 */
{
    if (Len >= 0) {
	AddCodeLine ("%.*s", Len, Line);
    } else {
	AddCodeLine ("%s", Line);
    }
}



/*****************************************************************************/
/*	     		    Inlined known functions			     */
/*****************************************************************************/



void g_strlen (unsigned flags, unsigned long val, unsigned offs)
/* Inline the strlen() function */
{
    /* We need a label in both cases */
    unsigned label = GetLocalLabel ();

    /* Two different encodings */
    if (flags & CF_CONST) {

	/* The address of the string is constant. Create the correct label name */
    	char* lbuf = GetLabelName (flags, val, offs);

	/* Generate the strlen code */
	AddCodeLine ("ldy #$FF");
	g_defcodelabel (label);
	AddCodeLine ("iny");
	AddCodeLine ("lda %s,y", lbuf);
	AddCodeLine ("bne %s", LocalLabelName (label));
       	AddCodeLine ("tax");
	AddCodeLine ("tya");

    } else {

       	/* Address not constant but in primary */
	if (CodeSizeFactor < 400) {
	    /* This is too much code, so call strlen instead of inlining */
    	    AddCodeLine ("jsr _strlen");
	} else {
	    /* Inline the function */
	    AddCodeLine ("sta ptr1");
	    AddCodeLine ("stx ptr1+1");
	    AddCodeLine ("ldy #$FF");
	    g_defcodelabel (label);
	    AddCodeLine ("iny");
	    AddCodeLine ("lda (ptr1),y");
	    AddCodeLine ("bne %s", LocalLabelName (label));
       	    AddCodeLine ("tax");
	    AddCodeLine ("tya");
     	}
    }
}



