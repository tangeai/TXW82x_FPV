#include <stddef.h>
#include <string.h>
#include <iconv.h>

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    (void)tocode; (void)fromcode;
    return (void*)1;
}
size_t iconv(iconv_t cd, char **inbuf, size_t *inleft,
             char **outbuf, size_t *outleft)
{
    size_t n;
    if(!cd || cd == (iconv_t)-1 || !inbuf || !*inbuf)
        return (size_t)-1;
    n = *inleft < *outleft ? *inleft : *outleft;
    if(n) memcpy(*outbuf, *inbuf, n);
    *inbuf += n; *inleft -= n;
    *outbuf += n; *outleft -= n;
    return 0;
}
int iconv_close(iconv_t cd) { (void)cd; return 0; }
