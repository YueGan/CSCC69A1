/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, char **args, unsigned long nargs)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result, len, flen;//len used for finding argument length for padding, flen is final length after padding
	size_t maxlen = 0;
	vaddr_t argptrs[nargs+1];//pointers to stack addresses of arguments copied out. argptrs[0] stores vaddr_t
	//of argument 0.
	//find maximum arg length
	for (int i = 0; i < (int)nargs; i++){
		if (strlen(args[i]) > maxlen)
			maxlen = strlen(args[i]);
	}
	char arg[maxlen];//argument to send to copyout after terminated and padded

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}
	argptrs[nargs] = '\0';//null terminate the final arg pointer
	args[nargs] = '\0';//null terminate the arguments
//	stack grows to lower addresses. Start by adding final argument, decreasing stack pointer and adding
//	second to last arg, etc.
	for (int i = (int)nargs - 1; i >= 0; i--) {
//Find length of current argument, add 1 for null terminate and pad result to a multiple of 4
		len = strlen(args[i])+1;
		if (len % 4){//if len not divisible by 4
			flen = len + (4 - (len % 4));//len%4 is extra remainder. 4-remainder takes it to next mult of 4
		}
		else{
			flen = len;
		}
//		kprintf("arg: %s len: %d flen %d", args[i], len, flen);
//create space for this argument, copy the arg, null terminate
		//arg = kmalloc((sizeof(char))*flen);
		for (int j = 0; j < len; j++){//copy arg as is, up to length of arg.
			arg[j] = args[i][j];
		}
		for (int j = len; j < flen; j++){//for final byte+padding, set to null for remaining len->flen spaces
			arg[j] = '\0';
		}
	//	move stackptr to accomodate the length of argument
		stackptr -= flen;
		argptrs[i] = stackptr;
	//	kprintf("%d\t", stackptr);
	//	kprintf("copying %s\n", arg);
	//	copy the arg to stackptr now that we've moved it to have enough room
		copyout(arg, (userptr_t)stackptr, (size_t)flen);
		//kfree(arg);//free argument before next iteration of arg copying

	}
	len = sizeof(argptrs);//make room and pad argument pointer array
	if (len % 4){
		flen = len + (4 - (len % 4));
	}
	else{
		flen = len;
	}
	stackptr -= flen;
	copyout(argptrs, (userptr_t)stackptr, (size_t)flen);


	/* Warp to user mode. */
	enter_new_process(nargs /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

