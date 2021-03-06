/*
 *  sha1.h
 *
 *  Description:
 *      This is the header file for code which implements the Secure
 *      Hashing Algorithm 1 as defined in FIPS PUB 180-1 published
 *      April 17, 1995.
 *
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *      Please read the file sha1.c for more information.
 *
 */
 
#ifndef _SHA1_H_
#define _SHA1_H_
 
#include <stdint.h>
 
#define SHA1HashSize 20
 
/*
 *  This structure will hold context information for the SHA-1
 *  hashing operation
 */
typedef struct SHA1Context {
    uint32_t Intermediate_Hash[5];              /* Message Digest  */
 
    uint32_t Length_Low;            /* Message length in bits      */
    uint32_t Length_High;           /* Message length in bits      */
 
                               /* Index into message block array   */
    int_least16_t Message_Block_Index;
    uint8_t Message_Block[64];      /* 512-bit message blocks      */
 
    int Computed;               /* Is the digest computed?         */
    int Corrupted;             /* Is the message digest corrupted? */
} SHA1Context;
 
/*
 *  Function Prototypes
 */
 
int SHA1Reset(SHA1Context *context);
int SHA1Input(SHA1Context *context, const uint8_t *message_array, unsigned int length);
int SHA1Result( SHA1Context *context, uint8_t *Message_Digest);
 
#endif