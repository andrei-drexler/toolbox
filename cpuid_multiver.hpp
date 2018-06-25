/***********************************************************************************************
cpuid_multiver.h - v0.1 (alpha) - github.com/andrei-drexler/toolbox - Public domain/MIT
************************************************************************************************
ABOUT:
	Header-only, C++98/11/17, CPUID-based function multiversioning helper library.
	For x86/x64, Windows/Linux, MSVC/GCC (maybe clang, too, but not tested yet).

USAGE:
	1) Declare a function pointer that exposes the functionality to the rest of the code:

		// [image.h]
		extern void (*rgb_to_bgr)(void* dst, const void* src, size_t num_pixels);

	2) In the implementation file, define all the different versions of your function.

	Note: when compiling with GCC, in order to be able to use intrinsics that are not available
	on the base project/file platform (e.g. SSSE3 intrinsics when compiling for SSE2), you'll have
	to annotate the specialized functions with __attribute__((target(<target-options>))
	(see https://gcc.gnu.org/onlinedocs/gcc/x86-Function-Attributes.html).
	To make it easier to write cross-platform code, this library defines a CMV_GCC_TARGET(t) macro
	that expands to __attribute__((target(t))) on GCC and is ignored otherwise.

		// [image.cpp]
		#include "image.h"
		#include "cpuid_multiver.h"

		CMV_GCC_TARGET("ssse3")
		static void rgb_to_bgr_ssse3(void* dst, const void* src, size_t num_pixels) {
			// pshufb ftw
		}

		static void rgb_to_bgr_generic(void* dst, const void* src, size_t num_pixels) {
			// slower, generic code
		}

	3) Define an array of cmv::version<function-pointer-type> listing all the function versions
	and their requirements, from most specialized to most generic. Each entry in the array
	consists of a function pointer and a mask of all the required capabilities (cmv::caps) OR-ed together.
	The last entry in the array *MUST* use cmv::generic (aka 0) as its requirement mask.
	Note: depending on your choice on the next step, the array might need external linkage.

		// [image.cpp, cont'd]
		extern const cmv::version<decltype(rgb_to_bgr)> rgb_to_bgr_versions[] = {
			{rgb_to_bgr_ssse3,		cmv::ssse3|cmv::sse2|cmv::sse},
			{rgb_to_bgr_generic,	cmv::generic},
		};

	4) Finally, define the function pointer declared in step 1.
	You have two options, depending on when you want the function pointer to be resolved:

		a) Resolve during CRT startup (before main is called).
		In this case, initialize the function pointer with the value returned by cmv::resolve(array-of_versions):

			// [image.cpp, cont'd]
			void (*rgb_to_bgr)(void*, const void*, size_t) rgb_to_bgr = cmv::resolve(rgb_to_bgr_versions);
			                                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

		Note: for this type of initialization the version array doesn't need external linkage.

		b) Resolve on the first call made through the function pointer (lazy resolve) - C++11 or newer required.
		In this case, initialize the function pointer with the macro
			CMV_LAZY_RESOLVE(function-pointer, array-of-versions)
		or, if you have access to C++17, you can also use the value
			cmv::lazy_resolve<address-of-function-pointer, array-of-versions>:

			// [image.cpp, cont'd]
			void (*rgb_to_bgr)(void*, const void*, size_t) rgb_to_bgr = CMV_LAZY_RESOLVE(rgb_to_bgr, rgb_to_bgr_versions);
			                                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			or

			// C++17: note the & - we're passing the _address_ of our function pointer to the lazy_resolve template
			void (*rgb_to_bgr)(void*, const void*, size_t) rgb_to_bgr = cmv::lazy_resolve<&rgb_to_bgr, rgb_to_bgr_versions>;
			                                                            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	
		Note: for this type of initialization the version array *MUST* have external linkage,
		since its address is used as a template argument for the lazy resolver function.
	
	5) ???
	6) Profit!

TL;DR:
	// [image.h]
	extern void (*rgb_to_bgr)(void* dst, const void* src, size_t num_pixels);

	// [image.cpp]
	#include "image.h"
	#include "cpuid_multiver.h"

	CMV_GCC_TARGET("ssse3")
	static void rgb_to_bgr_ssse3(void* dst, const void* src, size_t num_pixels) {
		// pshufb ftw
	}

	static void rgb_to_bgr_generic(void* dst, const void* src, size_t num_pixels) {
		// slower, generic code
	}

	extern const cmv::version<decltype(rgb_to_bgr)> rgb_to_bgr_versions[] = {
		{rgb_to_bgr_ssse3,		cmv::caps::ssse3|cmv::caps::sse2|cmv::caps::sse},
		{rgb_to_bgr_generic,	cmv::caps::generic},
	};

	// resolve during CRT startup, C++98
	void (*rgb_to_bgr)(void*, const void*, size_t) rgb_to_bgr = cmv::resolve(rgb_to_bgr_versions);

	// OR: lazy resolve (on first call), C++11
	void (*rgb_to_bgr)(void*, const void*, size_t) rgb_to_bgr = CMV_LAZY_RESOLVE(rgb_to_bgr, rgb_to_bgr_versions);

	// OR: lazy resolve (on first call), C++17
	void (*rgb_to_bgr)(void*, const void*, size_t) rgb_to_bgr = cmv::lazy_resolve<&rgb_to_bgr, rgb_to_bgr_versions>;

	// [myapp.cpp]
	#include "image.h"

	void profit(...) {
		rgb_to_bgr(dst, src, num_pixels);
	}

LICENSE:
	MIT or public domain, whichever you prefer (see end of file).
***********************************************************************************************/

#ifndef CMV_HPP_INCLUDED
#define CMV_HPP_INCLUDED

#ifndef CMV_CPP_VERSION
	#if defined(_MSC_VER) && defined(_MSVC_LANG)
		#define CMV_CPP_VERSION _MSVC_LANG
	#else
		#define CMV_CPP_VERSION __cplusplus
	#endif
#endif // ndef CMV_CPP_VERSION

#define CMV__CONCAT2(a, b) a ## b
#define CMV__CONCAT(a, b) CMV__CONCAT2(a, b)

#if CMV_CPP_VERSION >= 201103L
	#define CMV_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
	#define CMV_STATIC_ASSERT(cond, msg)	\
		struct CMV__CONCAT(static_assertion_on_line_, __LINE__) { int CMV__CONCAT(static_assertion_failed_on_line_, __LINE__) : !!(cond); }
#endif // CMV_CPP_VERSION >= 201103L

////////////////////////////////////////////////////////////////

namespace cmv {
	typedef unsigned int caps_storage;

	#define CMV_CAPABILITY_LIST(x)          \
	    x(mmx,          1,  4,      23)     \
	    x(sse,          1,  4,      25)     \
	    x(sse2,         1,  4,      26)     \
	    x(sse3,         1,  3,       0)     \
	    x(ssse3,        1,  3,       9)     \
	    x(sse41,        1,  3,      19)     \
	    x(sse42,        1,  3,      20)     \
	    x(bmi1,         7,  2,       3)     \
	    x(bmi2,         7,  2,       8)     \
	    x(aes,          1,  3,      25)     \
	    x(f16c,         1,  3,      29)     \
	    x(avx,          1,  3,      28)     \
	    x(avx2,         7,  2,       5)     \
	    x(pclmulqdq,    1,  3,       1)     \
	    x(rdrand,       1,  3,      30)     \
	    x(rdseed,       7,  2,      18)     \
	//  x(name,         fn, reg,    regbit)

	namespace capability_index {
		enum {
			#define CMV_ADD_CAP_INDEX(name, fn, reg, regbit)	name,
			CMV_CAPABILITY_LIST(CMV_ADD_CAP_INDEX)
			#undef CMV_ADD_CAP_INDEX
			count,
		};
	}
	CMV_STATIC_ASSERT(capability_index::count <= sizeof(caps_storage) * 8, "not enough bits in caps_storage");
	
	enum caps {
		generic = 0,

		#define CMV_ADD_CAP_BIT(name, fn, reg, regbit)	\
			name = 1 << capability_index::name,
		CMV_CAPABILITY_LIST(CMV_ADD_CAP_BIT)
		#undef CMV_ADD_CAP_BIT
	};

	caps detect_system_caps();
	inline caps get_cached_system_caps() {
		static caps cached = detect_system_caps();
		return cached;
	}

	////////////////////////////////////////////////////////////////

	template <typename FunctionPointer>
	struct version {
		FunctionPointer		function;
		caps_storage		requirements;
	};

	template <typename FunctionPointer>
	FunctionPointer resolve(const version<FunctionPointer>*const first_candidate) {
		caps_storage not_present = ~get_cached_system_caps();
		const version<FunctionPointer>* candidate = first_candidate;
		while (candidate->requirements & not_present)
			++candidate;
		return candidate->function;
	}

#if CMV_CPP_VERSION >= 201103L
	template <typename FunctionPointer>
	struct lazy;

	template <typename T>
	struct lazy<T&> : lazy<T> { };

	template <typename Return, typename... Arguments>
	struct lazy<Return(*)(Arguments...)> {
		typedef Return(*function_pointer)(Arguments...);

		template <function_pointer* Destination, const version<function_pointer>*const Versions>
		static Return resolve(Arguments... args) {
			*Destination = cmv::resolve(Versions);
			return (*Destination)(static_cast<Arguments&&>(args)...);
		}
	};

#if defined(_WIN32) && defined(_M_IX86) && !defined(_M_X64)
	// stdcall function pointers
	template <typename Return, typename... Arguments>
	struct lazy<Return(__stdcall*)(Arguments...)> {
		typedef Return(__stdcall*function_pointer)(Arguments...);

		template <function_pointer* Destination, const version<function_pointer>*const Versions>
		static Return __stdcall resolve(Arguments... args) {
			*Destination = cmv::resolve(Versions);
			return (*Destination)(static_cast<Arguments&&>(args)...);
		}
	};
#endif

#define CMV_LAZY_RESOLVE(func, versions) \
	&cmv::lazy<decltype(func)>::template resolve<&func, versions>

#endif // CMV_CPP_VERSION >= 201103L

#if CMV_CPP_VERSION >= 201703L
	template <auto* Destination, auto* Versions>
	constexpr auto lazy_resolve = &lazy<decltype(*Destination)>::template resolve<Destination, Versions>;
#endif // CMV_CPP_VERSION >= 201703L
} // end of namespace cmv

////////////////////////////////////////////////////////////////
// CPUID support ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
	#include <intrin.h>

	inline cmv::caps cmv::detect_system_caps() {
		caps_storage result = 0;

		int f1[4] = {0};
		int f7[4] = {0};

		int data[4];
		__cpuid(data, 0);
		int max_func = data[0];
		if (max_func >= 1) __cpuidex(f1, 1, 0);
		if (max_func >= 7) __cpuidex(f7, 7, 0);

		#define CMV_DETECT_CAP(name, fn, reg, regbit)	\
			if (f##fn[reg-1] & (1<<regbit)) result |= static_cast<cmv::caps_storage>(cmv::name);
		CMV_CAPABILITY_LIST(CMV_DETECT_CAP)
		#undef CMV_DETECT_CAP

		return static_cast<caps>(result);
	}
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	#include <cpuid.h>

	inline cmv::caps cmv::detect_system_caps() {
		caps_storage result = 0;

		unsigned int f1[4] = {0};
		unsigned int f7[4] = {0};

		int max_func = __get_cpuid_max(0, 0);
		if (max_func >= 1) __get_cpuid(1, f1+0, f1+1, f1+2, f1+3);
		if (max_func >= 7) __get_cpuid(7, f7+0, f7+1, f7+2, f7+3);

		#define CMV_DETECT_CAP(name, fn, reg, regbit)	\
			if (f##fn[reg-1] & (1<<regbit)) result |= static_cast<cmv::caps_storage>(cmv::name);
		CMV_CAPABILITY_LIST(CMV_DETECT_CAP)
		#undef CMV_DETECT_CAP

		return static_cast<caps>(result);
	}
#else
	#error Unsupported platform
#endif

////////////////////////////////////////////////////////////////

#if defined(__GNUC__)
	#define CMV_GCC_TARGET(name)	__attribute__((target(name)))
#else
	#define CMV_GCC_TARGET(name)
#endif

#endif // ndef CMV_HPP_INCLUDED

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2018 Andrei Drexler
Permission is hereby granted, free of charge, to any person obtaining a copy of 
this software and associated documentation files (the "Software"), to deal in 
the Software without restriction, including without limitation the rights to 
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
of the Software, and to permit persons to whom the Software is furnished to do 
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this 
software, either in source code form or as a compiled binary, for any purpose, 
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this 
software dedicate any and all copyright interest in the software to the public 
domain. We make this dedication for the benefit of the public at large and to 
the detriment of our heirs and successors. We intend this dedication to be an 
overt act of relinquishment in perpetuity of all present and future rights to 
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
