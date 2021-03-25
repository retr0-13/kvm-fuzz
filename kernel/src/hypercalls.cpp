#include <stdarg.h>
#include "hypercalls.h"

// Keep this the same as in the hypervisor!
enum Hypercall : size_t {
	Test,
	Print,
	GetMemInfo,
	GetKernelBrk,
	GetInfo,
	GetFileLen,
	GetFileName,
	SetFileBuf,
	Fault,
	PrintStacktrace,
	EndRun,
};

// This is traduced to:
//    mov eax, `n`;
//    out 16, al;
//    ret;
// That's the body of each hypercall. Arguments are passed via rdi, rsi, etc,
// and return value is put in rax by the hypervisor.

#define hypercall(n)    \
	asm volatile(       \
		"out 16, al;"   \
		"ret;"          \
		:               \
		: "a" (n)       \
		:               \
	)

__attribute__((naked))
void hc_test(size_t arg) {
	hypercall(Hypercall::Test);
}

__attribute__((naked))
void hc_print(const char* msg) {
	hypercall(Hypercall::Print);
}

void hc_print(const char* buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		hc_print(buf[i]);
	}
}

const size_t out_buf_size = 1024;
char out_buf[out_buf_size];
size_t used = 0;
void hc_print(char c) {
	// Add the char to the buffer. Print only if it's a line break or if the
	// buffer is full
	out_buf[used++] = c;
	if (c == '\n' || used == out_buf_size-1) {
		out_buf[used] = 0;
		hc_print(out_buf);
		used = 0;
	}
}

void hc_print(const string& msg) {
	hc_print(msg.c_str());
}

__attribute__((naked))
void hc_get_mem_info(void** mem_start, size_t* mem_length) {
	hypercall(Hypercall::GetMemInfo);
}

__attribute__((naked))
void* hc_get_kernel_brk() {
	hypercall(Hypercall::GetKernelBrk);
}

__attribute__((naked))
void hc_get_info(VmInfo* info) {
	hypercall(Hypercall::GetInfo);
}

__attribute__((naked))
size_t hc_get_file_len(size_t n) {
	hypercall(Hypercall::GetFileLen);
}

__attribute__((naked))
void hc_get_file_name(size_t n, char* buf) {
	hypercall(Hypercall::GetFileName);
}

__attribute__((naked))
void hc_set_file_buf(size_t n, void* buf) {
	hypercall(Hypercall::SetFileBuf);
}

__attribute__((naked))
void hc_fault(FaultInfo* fault) {
	hypercall(Hypercall::Fault);
}

__attribute__((naked))
void hc_print_stacktrace(uint64_t rsp, uint64_t rip) {
	hypercall(Hypercall::PrintStacktrace);
}

__attribute__((naked))
void hc_end_run() {
	hypercall(Hypercall::EndRun);
}