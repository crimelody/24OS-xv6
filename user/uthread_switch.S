	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
	 *
	 * void thread_switch(uint64 old_ctx, uint64 new_ctx);
	 * a0 = &old_thread->context, a1 = &new_thread->context
         */

	.globl thread_switch
thread_switch:
	/* 保存当前线程（old）的 callee-saved 寄存器到 a0 指向的 context */
	sd ra, 0(a0)
	sd sp, 8(a0)
	sd s0, 16(a0)
	sd s1, 24(a0)
	sd s2, 32(a0)
	sd s3, 40(a0)
	sd s4, 48(a0)
	sd s5, 56(a0)
	sd s6, 64(a0)
	sd s7, 72(a0)
	sd s8, 80(a0)
	sd s9, 88(a0)
	sd s10, 96(a0)
	sd s11, 104(a0)

	/* 从 a1 指向的 context 恢复新线程（next）的寄存器 */
	ld ra, 0(a1)
	ld sp, 8(a1)
	ld s0, 16(a1)
	ld s1, 24(a1)
	ld s2, 32(a1)
	ld s3, 40(a1)
	ld s4, 48(a1)
	ld s5, 56(a1)
	ld s6, 64(a1)
	ld s7, 72(a1)
	ld s8, 80(a1)
	ld s9, 88(a1)
	ld s10, 96(a1)
	ld s11, 104(a1)

	ret    /* 跳到新线程 context->ra：若是新建线程则是其函数入口 */
