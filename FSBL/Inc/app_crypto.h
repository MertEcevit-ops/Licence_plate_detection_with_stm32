#ifndef APP_CRYPTO_H
#define APP_CRYPTO_H

#include "app_status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CRYPTO_AES128_KEY_SIZE 16U
#define APP_CRYPTO_AES_BLOCK_SIZE 16U
#define APP_CRYPTO_AES128_CTR_ROUND_KEY_SIZE 176U

typedef struct
{
  uint8_t RoundKey[APP_CRYPTO_AES128_CTR_ROUND_KEY_SIZE];
  uint8_t Counter[APP_CRYPTO_AES_BLOCK_SIZE];
  uint8_t StreamBlock[APP_CRYPTO_AES_BLOCK_SIZE];
  uint8_t StreamOffset;
} AppCrypto_Aes128CtrContext_t;

AppStatus_t AppCrypto_Aes128CtrStartFrame(AppCrypto_Aes128CtrContext_t *Context,
                                          uint32_t FrameId);
AppStatus_t AppCrypto_Aes128CtrCrypt(AppCrypto_Aes128CtrContext_t *Context,
                                     const uint8_t *Input,
                                     uint8_t *Output,
                                     uint32_t Size);

#ifdef __cplusplus
}
#endif

#endif /* APP_CRYPTO_H */
