/* minimal stand-in for xnu-792 mach/mach_types.h (the real tree is not on this Mac any more) */
#ifndef _STUB_MACH_TYPES_H
#define _STUB_MACH_TYPES_H
typedef int kern_return_t;
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
typedef unsigned int vm_address_t;
typedef unsigned int vm_size_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
#endif
