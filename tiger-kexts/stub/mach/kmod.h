/* minimal stand-in for xnu-792 mach/kmod.h: same kmod_info_t layout and KMOD_EXPLICIT_DECL as the real header */
#ifndef _STUB_KMOD_H
#define _STUB_KMOD_H
#include <mach/mach_types.h>
#define KMOD_MAX_NAME 64
#define KMOD_INFO_VERSION 1
struct kmod_reference;
typedef struct kmod_info kmod_info_t;
typedef kern_return_t kmod_start_func_t(kmod_info_t *ki, void *data);
typedef kern_return_t kmod_stop_func_t(kmod_info_t *ki, void *data);
struct kmod_info {
	struct kmod_info *next;
	int32_t info_version;
	uint32_t id;
	char name[KMOD_MAX_NAME];
	char version[KMOD_MAX_NAME];
	int32_t reference_count;
	struct kmod_reference *reference_list;
	vm_address_t address;
	vm_size_t size;
	vm_size_t hdr_size;
	kmod_start_func_t *start;
	kmod_stop_func_t *stop;
};
#define KMOD_EXPLICIT_DECL(name, version, start, stop) \
	__attribute__((visibility("default"))) \
	kmod_info_t kmod_info = { 0, KMOD_INFO_VERSION, -1, { #name }, { version }, -1, 0, 0, 0, 0, start, stop };
#endif
