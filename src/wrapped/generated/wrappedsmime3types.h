/*******************************************************************
 * File automatically generated by rebuild_wrappers.py (v2.1.0.16) *
 *******************************************************************/
#ifndef __wrappedsmime3TYPES_H_
#define __wrappedsmime3TYPES_H_

#ifndef LIBNAME
#error You should only #include this file inside a wrapped*.c file
#endif
#ifndef ADDED_FUNCTIONS
#define ADDED_FUNCTIONS() 
#endif

typedef int64_t (*iFpp_t)(void*, void*);
typedef int64_t (*iFppp_t)(void*, void*, void*);
typedef void* (*pFpppp_t)(void*, void*, void*, void*);
typedef void* (*pFpppppppp_t)(void*, void*, void*, void*, void*, void*, void*, void*);
typedef void* (*pFppppppppppp_t)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*, void*);

#define SUPER() ADDED_FUNCTIONS() \
	GO(SEC_PKCS12DecoderValidateBags, iFpp_t) \
	GO(SEC_PKCS12Encode, iFppp_t) \
	GO(SEC_PKCS12CreateExportContext, pFpppp_t) \
	GO(SEC_PKCS12DecoderStart, pFpppppppp_t) \
	GO(NSS_CMSEncoder_Start, pFppppppppppp_t)

#endif // __wrappedsmime3TYPES_H_