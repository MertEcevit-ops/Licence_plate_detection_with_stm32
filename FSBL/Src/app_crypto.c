#include "app_crypto.h"
#include <stddef.h>
#include <string.h>

#define AES128_ROUNDS 10U

static const uint8_t Aes128Key[APP_CRYPTO_AES128_KEY_SIZE] =
{
  0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
  0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

static const uint8_t AesCtrNonce[8] =
{
  0x4C, 0x50, 0x44, 0x45, 0x54, 0x45, 0x43, 0x54
};

static const uint8_t Sbox[256] =
{
  0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
  0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
  0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
  0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
  0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
  0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
  0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
  0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
  0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
  0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
  0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
  0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
  0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
  0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
  0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
  0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

static const uint8_t Rcon[11] =
{
  0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

static uint8_t Xtime(uint8_t Value)
{
  return (uint8_t)((Value << 1U) ^ (((Value >> 7U) & 1U) * 0x1BU));
}

static void KeyExpansion(uint8_t *RoundKey, const uint8_t *Key)
{
  uint32_t bytes_generated = APP_CRYPTO_AES128_KEY_SIZE;
  uint8_t rcon_iteration = 1U;
  uint8_t temp[4];
  uint8_t i;

  memcpy(RoundKey, Key, APP_CRYPTO_AES128_KEY_SIZE);

  while (bytes_generated < APP_CRYPTO_AES128_CTR_ROUND_KEY_SIZE)
  {
    for (i = 0U; i < 4U; i++)
    {
      temp[i] = RoundKey[bytes_generated - 4U + i];
    }

    if ((bytes_generated % APP_CRYPTO_AES128_KEY_SIZE) == 0U)
    {
      uint8_t rotated = temp[0];
      temp[0] = Sbox[temp[1]] ^ Rcon[rcon_iteration++];
      temp[1] = Sbox[temp[2]];
      temp[2] = Sbox[temp[3]];
      temp[3] = Sbox[rotated];
    }

    for (i = 0U; i < 4U; i++)
    {
      RoundKey[bytes_generated] = RoundKey[bytes_generated - APP_CRYPTO_AES128_KEY_SIZE] ^ temp[i];
      bytes_generated++;
    }
  }
}

static void AddRoundKey(uint8_t *State, const uint8_t *RoundKey, uint8_t Round)
{
  uint8_t i;

  for (i = 0U; i < APP_CRYPTO_AES_BLOCK_SIZE; i++)
  {
    State[i] ^= RoundKey[((uint32_t)Round * APP_CRYPTO_AES_BLOCK_SIZE) + i];
  }
}

static void SubBytes(uint8_t *State)
{
  uint8_t i;

  for (i = 0U; i < APP_CRYPTO_AES_BLOCK_SIZE; i++)
  {
    State[i] = Sbox[State[i]];
  }
}

static void ShiftRows(uint8_t *State)
{
  uint8_t tmp[APP_CRYPTO_AES_BLOCK_SIZE];

  tmp[0] = State[0];
  tmp[4] = State[4];
  tmp[8] = State[8];
  tmp[12] = State[12];

  tmp[1] = State[5];
  tmp[5] = State[9];
  tmp[9] = State[13];
  tmp[13] = State[1];

  tmp[2] = State[10];
  tmp[6] = State[14];
  tmp[10] = State[2];
  tmp[14] = State[6];

  tmp[3] = State[15];
  tmp[7] = State[3];
  tmp[11] = State[7];
  tmp[15] = State[11];

  memcpy(State, tmp, APP_CRYPTO_AES_BLOCK_SIZE);
}

static void MixColumns(uint8_t *State)
{
  uint8_t i;

  for (i = 0U; i < APP_CRYPTO_AES_BLOCK_SIZE; i += 4U)
  {
    uint8_t t = State[i];
    uint8_t tmp = State[i] ^ State[i + 1U] ^ State[i + 2U] ^ State[i + 3U];
    uint8_t tm = State[i] ^ State[i + 1U];
    tm = Xtime(tm);
    State[i] ^= tm ^ tmp;

    tm = State[i + 1U] ^ State[i + 2U];
    tm = Xtime(tm);
    State[i + 1U] ^= tm ^ tmp;

    tm = State[i + 2U] ^ State[i + 3U];
    tm = Xtime(tm);
    State[i + 2U] ^= tm ^ tmp;

    tm = State[i + 3U] ^ t;
    tm = Xtime(tm);
    State[i + 3U] ^= tm ^ tmp;
  }
}

static void Aes128EncryptBlock(const uint8_t *RoundKey,
                               const uint8_t *Input,
                               uint8_t *Output)
{
  uint8_t state[APP_CRYPTO_AES_BLOCK_SIZE];
  uint8_t round;

  memcpy(state, Input, APP_CRYPTO_AES_BLOCK_SIZE);
  AddRoundKey(state, RoundKey, 0U);

  for (round = 1U; round < AES128_ROUNDS; round++)
  {
    SubBytes(state);
    ShiftRows(state);
    MixColumns(state);
    AddRoundKey(state, RoundKey, round);
  }

  SubBytes(state);
  ShiftRows(state);
  AddRoundKey(state, RoundKey, AES128_ROUNDS);

  memcpy(Output, state, APP_CRYPTO_AES_BLOCK_SIZE);
}

static void IncrementCounter(uint8_t *Counter)
{
  int32_t index;

  for (index = (int32_t)APP_CRYPTO_AES_BLOCK_SIZE - 1; index >= 0; index--)
  {
    Counter[index]++;
    if (Counter[index] != 0U)
    {
      break;
    }
  }
}

AppStatus_t AppCrypto_Aes128CtrStartFrame(AppCrypto_Aes128CtrContext_t *Context,
                                          uint32_t FrameId)
{
  if (Context == NULL)
  {
    return APP_STATUS_INVALID_ARG;
  }

  KeyExpansion(Context->RoundKey, Aes128Key);
  memcpy(Context->Counter, AesCtrNonce, sizeof(AesCtrNonce));
  Context->Counter[8] = (uint8_t)((FrameId >> 24) & 0xFFU);
  Context->Counter[9] = (uint8_t)((FrameId >> 16) & 0xFFU);
  Context->Counter[10] = (uint8_t)((FrameId >> 8) & 0xFFU);
  Context->Counter[11] = (uint8_t)(FrameId & 0xFFU);
  Context->Counter[12] = 0U;
  Context->Counter[13] = 0U;
  Context->Counter[14] = 0U;
  Context->Counter[15] = 0U;
  memset(Context->StreamBlock, 0, sizeof(Context->StreamBlock));
  Context->StreamOffset = APP_CRYPTO_AES_BLOCK_SIZE;

  return APP_STATUS_OK;
}

AppStatus_t AppCrypto_Aes128CtrCrypt(AppCrypto_Aes128CtrContext_t *Context,
                                     const uint8_t *Input,
                                     uint8_t *Output,
                                     uint32_t Size)
{
  uint32_t i;

  if ((Context == NULL) || (Output == NULL) || ((Input == NULL) && (Size != 0U)))
  {
    return APP_STATUS_INVALID_ARG;
  }

  for (i = 0U; i < Size; i++)
  {
    if (Context->StreamOffset >= APP_CRYPTO_AES_BLOCK_SIZE)
    {
      Aes128EncryptBlock(Context->RoundKey, Context->Counter, Context->StreamBlock);
      IncrementCounter(Context->Counter);
      Context->StreamOffset = 0U;
    }

    Output[i] = Input[i] ^ Context->StreamBlock[Context->StreamOffset];
    Context->StreamOffset++;
  }

  return APP_STATUS_OK;
}
