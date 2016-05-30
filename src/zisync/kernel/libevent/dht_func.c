/**
 * @file dht_func.c
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief function called by dht.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifndef _MSC_VER
	#include <unistd.h>
	#include <sys/socket.h>
#else 
  #include <openssl/rand.h>
#endif

#if defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#include <CommonCrypto/CommonDigest.h>
  #ifdef MD5_DIGEST_LENGTH
    #undef MD5_DIGEST_LENGTH
  #endif

  #define MD5_Init CC_MD5_Init
  #define MD5_Update CC_MD5_Update
  #define MD5_Final CC_MD5_Final
  #define MD5_DIGEST_LENGTH CC_MD5_DIGEST_LENGTH
  #define MD5_CTX CC_MD5_CTX

#else
#include <openssl/md5.h>
#endif


int
dht_blacklisted(const struct sockaddr *sa, int salen)
{
    return 0;
}

/* We need to provide a reasonably strong cryptographic hashing function.
   Here's how we'd do it if we had RSA's MD5 code. */
#if 1
void
dht_hash(void *hash_return, int hash_size,
         const void *v1, int len1,
         const void *v2, int len2,
         const void *v3, int len3)
{
    unsigned char md[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, v1, len1);
    MD5_Update(&ctx, v2, len2);
    MD5_Update(&ctx, v3, len3);
    MD5_Final(md, &ctx);
    if(hash_size > 16)
        memset((char*)hash_return + 16, 0, hash_size - 16);
    memcpy(hash_return, md, hash_size > 16 ? 16 : hash_size);
}
#else
/* But for this example, we might as well use something weaker. */
void
dht_hash(void *hash_return, int hash_size,
         const void *v1, int len1,
         const void *v2, int len2,
         const void *v3, int len3)
{
    const char *c1 = v1, *c2 = v2, *c3 = v3;
    char key[9];                /* crypt is limited to 8 characters */
    int i;

    memset(key, 0, 9);
#define CRYPT_HAPPY(c) ((c % 0x60) + 0x20)

    for(i = 0; i < 2 && i < len1; i++)
        key[i] = CRYPT_HAPPY(c1[i]);
    for(i = 0; i < 4 && i < len1; i++)
        key[2 + i] = CRYPT_HAPPY(c2[i]);
    for(i = 0; i < 2 && i < len1; i++)
        key[6 + i] = CRYPT_HAPPY(c3[i]);
    strncpy(hash_return, crypt(key, "jc"), hash_size);
}
#endif

#ifndef _MSC_VER
int
dht_random_bytes(void *buf, size_t size)
{
    int fd, rc, save;

    fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0)
        return -1;

    rc = read(fd, buf, size);

    save = errno;
    close(fd);
    errno = save;

    return rc;
}
#else 
int
  dht_random_bytes(void *buf, size_t size)
{
  if (RAND_bytes((unsigned char*)buf, (int)size)) {
    return (int)size;
  }
  return -1;
}
#endif

