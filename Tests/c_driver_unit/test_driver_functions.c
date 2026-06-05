#include "app_crypto.h"
#include "app_memory.h"
#include "app_resources.h"
#include "app_transport_uart.h"
#include "snapshot_protocol.h"
#include "main.h"
#include "stm32n6570_discovery.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(values) (sizeof(values) / sizeof((values)[0]))
#define MAX_UART_TX_CALLS 8U
#define UART_CAPTURE_SIZE (SNAPSHOT_PROTOCOL_HEADER_SIZE + APP_UART_TX_CHUNK_SIZE + 512U)

typedef void (*TestFunction_t)(void);

typedef struct
{
  const char *Name;
  TestFunction_t Function;
} TestCase_t;

typedef struct
{
  uint16_t Size;
  uint8_t Data[UART_CAPTURE_SIZE];
} UartTxCall_t;

static uint32_t TestsRun;
static uint32_t TestsPassed;
static const char *CurrentTestName;
static int CurrentTestFailed;

static UartTxCall_t UartCalls[MAX_UART_TX_CALLS];
static uint32_t UartCallCount;
static HAL_StatusTypeDef UartTransmitResult = HAL_OK;

static AppStatus_t LockResult = APP_STATUS_OK;
static AppResourceId_t LockFailResource = APP_RESOURCE_COUNT;
static AppResourceId_t LockLog[8];
static AppResourceId_t UnlockLog[8];
static uint32_t LockLogCount;
static uint32_t UnlockLogCount;

static uint32_t DCacheInvalidateCount;
static uintptr_t DCacheLastAddress;
static int32_t DCacheLastSize;
static uint8_t TestFrameBuffer[FRAME_BUFFER_SIZE];

UART_HandleTypeDef hcom_uart[COMn];

static void FailAtLine(int Line, const char *Expression)
{
  if (CurrentTestFailed == 0)
  {
    printf("[FAIL] %s:%d: %s\n", CurrentTestName, Line, Expression);
  }
  CurrentTestFailed = 1;
}

#define EXPECT_TRUE(expression) \
  do \
  { \
    if (!(expression)) \
    { \
      FailAtLine(__LINE__, #expression); \
    } \
  } while (0)

#define EXPECT_EQ_U32(expected, actual) \
  do \
  { \
    uint32_t expected_value = (uint32_t)(expected); \
    uint32_t actual_value = (uint32_t)(actual); \
    if (expected_value != actual_value) \
    { \
      printf("[FAIL] %s:%d: expected 0x%08lX, got 0x%08lX\n", \
             CurrentTestName, __LINE__, (unsigned long)expected_value, \
             (unsigned long)actual_value); \
      CurrentTestFailed = 1; \
    } \
  } while (0)

#define EXPECT_EQ_STATUS(expected, actual) EXPECT_EQ_U32((expected), (actual))

#define EXPECT_MEM_EQ(expected, actual, size) \
  do \
  { \
    if (memcmp((expected), (actual), (size)) != 0) \
    { \
      printf("[FAIL] %s:%d: memory mismatch over %lu bytes\n", \
             CurrentTestName, __LINE__, (unsigned long)(size)); \
      CurrentTestFailed = 1; \
    } \
  } while (0)

void SCB_InvalidateDCache_by_Addr(uint32_t *addr, int32_t dsize)
{
  DCacheInvalidateCount++;
  DCacheLastAddress = (uintptr_t)addr;
  DCacheLastSize = dsize;
}

AppStatus_t AppResources_Init(void)
{
  return APP_STATUS_OK;
}

AppStatus_t AppResources_Lock(AppResourceId_t Resource, TickType_t Timeout)
{
  (void)Timeout;

  if (LockLogCount < ARRAY_LEN(LockLog))
  {
    LockLog[LockLogCount] = Resource;
  }
  LockLogCount++;

  if ((LockResult != APP_STATUS_OK) &&
      ((LockFailResource == APP_RESOURCE_COUNT) || (LockFailResource == Resource)))
  {
    return LockResult;
  }

  return APP_STATUS_OK;
}

void AppResources_Unlock(AppResourceId_t Resource)
{
  if (UnlockLogCount < ARRAY_LEN(UnlockLog))
  {
    UnlockLog[UnlockLogCount] = Resource;
  }
  UnlockLogCount++;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    const uint8_t *pData,
                                    uint16_t Size,
                                    uint32_t Timeout)
{
  (void)huart;
  (void)Timeout;

  if (UartCallCount < MAX_UART_TX_CALLS)
  {
    UartCalls[UartCallCount].Size = Size;
    EXPECT_TRUE(Size <= UART_CAPTURE_SIZE);
    if (Size <= UART_CAPTURE_SIZE)
    {
      memcpy(UartCalls[UartCallCount].Data, pData, Size);
    }
  }
  UartCallCount++;

  return UartTransmitResult;
}

uint8_t *TestFramePointer(uint32_t Address)
{
  if ((Address < BUFFER_ADDRESS) ||
      (Address >= (BUFFER_ADDRESS + FRAME_BUFFER_SIZE)))
  {
    return NULL;
  }

  return &TestFrameBuffer[Address - BUFFER_ADDRESS];
}

static void ResetMocks(void)
{
  memset(UartCalls, 0, sizeof(UartCalls));
  UartCallCount = 0U;
  UartTransmitResult = HAL_OK;

  LockResult = APP_STATUS_OK;
  LockFailResource = APP_RESOURCE_COUNT;
  memset(LockLog, 0, sizeof(LockLog));
  memset(UnlockLog, 0, sizeof(UnlockLog));
  LockLogCount = 0U;
  UnlockLogCount = 0U;

  DCacheInvalidateCount = 0U;
  DCacheLastAddress = 0U;
  DCacheLastSize = 0;
}

static uint32_t LoadLe16(const uint8_t *Buffer)
{
  return ((uint32_t)Buffer[0]) | ((uint32_t)Buffer[1] << 8);
}

static uint32_t LoadLe32(const uint8_t *Buffer)
{
  return ((uint32_t)Buffer[0]) |
         ((uint32_t)Buffer[1] << 8) |
         ((uint32_t)Buffer[2] << 16) |
         ((uint32_t)Buffer[3] << 24);
}

static void FillFrameBuffer(uint32_t Size)
{
  uint32_t index;

  for (index = 0U; index < Size; index++)
  {
    TestFrameBuffer[index] = (uint8_t)(index & 0xFFU);
  }
}

static void TestSnapshotHeaderValidFrame(void)
{
  uint8_t header[SNAPSHOT_PROTOCOL_HEADER_SIZE];
  SnapshotProtocol_FrameInfo_t info = {
    .Width = SNAPSHOT_WIDTH,
    .Height = SNAPSHOT_HEIGHT,
    .BytesPerPixel = 2U,
    .Decimation = SNAPSHOT_DECIMATION_FACTOR,
    .PixelFormat = SNAPSHOT_PROTOCOL_PIXEL_FORMAT_RGB565,
    .Flags = SNAPSHOT_PROTOCOL_FLAG_AES128_CTR,
    .PayloadSize = SNAPSHOT_FRAME_BUFFER_SIZE,
    .FrameId = 7U,
  };

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   SnapshotProtocol_BuildHeader(header, sizeof(header), &info));

  EXPECT_MEM_EQ("SNAP", header, SNAPSHOT_PROTOCOL_MAGIC_SIZE);
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_VERSION, header[4]);
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_HEADER_SIZE, header[5]);
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_PIXEL_FORMAT_RGB565, header[6]);
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_FLAG_AES128_CTR, header[7]);
  EXPECT_EQ_U32(SNAPSHOT_WIDTH, LoadLe16(&header[8]));
  EXPECT_EQ_U32(SNAPSHOT_HEIGHT, LoadLe16(&header[10]));
  EXPECT_EQ_U32(2U, LoadLe16(&header[12]));
  EXPECT_EQ_U32(SNAPSHOT_DECIMATION_FACTOR, LoadLe16(&header[14]));
  EXPECT_EQ_U32(SNAPSHOT_FRAME_BUFFER_SIZE, LoadLe32(&header[16]));
  EXPECT_EQ_U32(7U, LoadLe32(&header[20]));
  EXPECT_EQ_U32(0U, LoadLe32(&header[24]));
  EXPECT_EQ_U32(0U, LoadLe32(&header[28]));
}

static void TestSnapshotHeaderRejectsInvalidInputs(void)
{
  uint8_t header[SNAPSHOT_PROTOCOL_HEADER_SIZE];
  SnapshotProtocol_FrameInfo_t info = {
    .Width = 2U,
    .Height = 2U,
    .BytesPerPixel = 2U,
    .Decimation = 1U,
    .PixelFormat = SNAPSHOT_PROTOCOL_PIXEL_FORMAT_RGB565,
    .Flags = 0U,
    .PayloadSize = 8U,
    .FrameId = 1U,
  };

  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   SnapshotProtocol_BuildHeader(NULL, sizeof(header), &info));
  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   SnapshotProtocol_BuildHeader(header, sizeof(header) - 1U, &info));
  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   SnapshotProtocol_BuildHeader(header, sizeof(header), NULL));

  info.Width = 0U;
  EXPECT_EQ_STATUS(APP_STATUS_PROTOCOL_ERROR,
                   SnapshotProtocol_BuildHeader(header, sizeof(header), &info));

  info.Width = 2U;
  info.BytesPerPixel = 1U;
  info.PayloadSize = 4U;
  EXPECT_EQ_STATUS(APP_STATUS_PROTOCOL_ERROR,
                   SnapshotProtocol_BuildHeader(header, sizeof(header), &info));

  info.BytesPerPixel = 2U;
  info.PayloadSize = 6U;
  EXPECT_EQ_STATUS(APP_STATUS_PROTOCOL_ERROR,
                   SnapshotProtocol_BuildHeader(header, sizeof(header), &info));
}

static void TestMemoryPayloadSizeAndBounds(void)
{
  EXPECT_EQ_U32(8U, AppMemory_Rgb565PayloadSize(2U, 2U));
  EXPECT_EQ_U32(SNAPSHOT_FRAME_BUFFER_SIZE,
                AppMemory_Rgb565PayloadSize(SNAPSHOT_WIDTH, SNAPSHOT_HEIGHT));
  EXPECT_EQ_U32(0U, AppMemory_Rgb565PayloadSize(0U, SNAPSHOT_HEIGHT));
  EXPECT_EQ_U32(0U, AppMemory_Rgb565PayloadSize(0xFFFFFFFFU, 3U));

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppMemory_ValidateFrameBuffer(BUFFER_ADDRESS, FRAME_BUFFER_SIZE));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppMemory_ValidateFrameBuffer(BUFFER_ADDRESS + 16U, 32U));
  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   AppMemory_ValidateFrameBuffer(BUFFER_ADDRESS, 0U));
  EXPECT_EQ_STATUS(APP_STATUS_SECURITY_ERROR,
                   AppMemory_ValidateFrameBuffer(BUFFER_ADDRESS - 1U, 1U));
  EXPECT_EQ_STATUS(APP_STATUS_SECURITY_ERROR,
                   AppMemory_ValidateFrameBuffer(BUFFER_ADDRESS + FRAME_BUFFER_SIZE - 1U, 2U));
}

static void TestMemoryDCacheInvalidationAlignment(void)
{
  ResetMocks();

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppMemory_InvalidateDCache(BUFFER_ADDRESS + 3U, 33U));
  EXPECT_EQ_U32(1U, DCacheInvalidateCount);
  EXPECT_EQ_U32(BUFFER_ADDRESS, DCacheLastAddress);
  EXPECT_EQ_U32(64U, (uint32_t)DCacheLastSize);

  EXPECT_EQ_STATUS(APP_STATUS_SECURITY_ERROR,
                   AppMemory_InvalidateDCache(BUFFER_ADDRESS - 4U, 16U));
  EXPECT_EQ_U32(1U, DCacheInvalidateCount);
}

static void TestCryptoRejectsInvalidInputs(void)
{
  AppCrypto_Aes128CtrContext_t context;
  uint8_t input[1] = {0U};
  uint8_t output[1] = {0U};

  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   AppCrypto_Aes128CtrStartFrame(NULL, 1U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrStartFrame(&context, 1U));
  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   AppCrypto_Aes128CtrCrypt(NULL, input, output, sizeof(input)));
  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   AppCrypto_Aes128CtrCrypt(&context, NULL, output, sizeof(input)));
  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   AppCrypto_Aes128CtrCrypt(&context, input, NULL, sizeof(input)));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&context, NULL, output, 0U));
}

static void TestCryptoKnownCtrVector(void)
{
  static const uint8_t expected[64] = {
    0x5D, 0xBE, 0x40, 0xC8, 0x9D, 0x55, 0x5F, 0xB3,
    0x86, 0x88, 0x09, 0x02, 0xBC, 0xB9, 0xFB, 0x53,
    0x70, 0x87, 0x07, 0xFD, 0x77, 0x74, 0x50, 0xE7,
    0x97, 0xDF, 0x14, 0x3F, 0xB2, 0x16, 0xB3, 0x78,
    0x11, 0x68, 0xC9, 0x6C, 0x26, 0xE8, 0x07, 0xC7,
    0xCE, 0x6B, 0xF6, 0x2A, 0x67, 0x8B, 0x32, 0x1E,
    0xF0, 0x08, 0xDD, 0x26, 0x2E, 0xAF, 0x99, 0xA7,
    0xFA, 0x40, 0x70, 0xAD, 0x95, 0xDB, 0x99, 0x97,
  };
  AppCrypto_Aes128CtrContext_t context;
  uint8_t input[64];
  uint8_t output[64];
  uint32_t index;

  for (index = 0U; index < sizeof(input); index++)
  {
    input[index] = (uint8_t)index;
  }

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrStartFrame(&context, 7U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&context, input, output, sizeof(input)));
  EXPECT_MEM_EQ(expected, output, sizeof(output));
}

static void TestCryptoChunkingMatchesOneShot(void)
{
  AppCrypto_Aes128CtrContext_t one_shot_context;
  AppCrypto_Aes128CtrContext_t chunked_context;
  uint8_t input[64];
  uint8_t one_shot[64];
  uint8_t chunked[64];
  uint8_t decrypted[64];
  uint32_t index;

  for (index = 0U; index < sizeof(input); index++)
  {
    input[index] = (uint8_t)(0xA0U + index);
  }

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrStartFrame(&one_shot_context, 3U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&one_shot_context, input, one_shot, sizeof(input)));

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrStartFrame(&chunked_context, 3U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&chunked_context, input, chunked, 5U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&chunked_context, &input[5], &chunked[5], 19U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&chunked_context, &input[24], &chunked[24], 40U));

  EXPECT_MEM_EQ(one_shot, chunked, sizeof(one_shot));

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrStartFrame(&chunked_context, 3U));
  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppCrypto_Aes128CtrCrypt(&chunked_context, one_shot, decrypted, sizeof(decrypted)));
  EXPECT_MEM_EQ(input, decrypted, sizeof(input));
}

static void TestUartTransportSendsEncryptedSnapshot(void)
{
  static const uint8_t expected_ciphertext[16] = {
    0xF1, 0xAB, 0x08, 0xCC, 0x00, 0xA7, 0xE4, 0x02,
    0xB3, 0xAF, 0xC4, 0xC9, 0x1E, 0x38, 0x4A, 0xAA,
  };

  ResetMocks();
  FillFrameBuffer(16U);

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS, 4U, 2U, 2U));

  EXPECT_EQ_U32(2U, UartCallCount);
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_HEADER_SIZE, UartCalls[0].Size);
  EXPECT_EQ_U32(16U, UartCalls[1].Size);
  EXPECT_MEM_EQ("SNAP", UartCalls[0].Data, SNAPSHOT_PROTOCOL_MAGIC_SIZE);
  EXPECT_EQ_U32(4U, LoadLe16(&UartCalls[0].Data[8]));
  EXPECT_EQ_U32(2U, LoadLe16(&UartCalls[0].Data[10]));
  EXPECT_EQ_U32(16U, LoadLe32(&UartCalls[0].Data[16]));
  EXPECT_EQ_U32(1U, LoadLe32(&UartCalls[0].Data[20]));
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_FLAG_AES128_CTR, UartCalls[0].Data[7]);
  EXPECT_MEM_EQ(expected_ciphertext, UartCalls[1].Data, sizeof(expected_ciphertext));
  EXPECT_EQ_U32(1U, DCacheInvalidateCount);
  EXPECT_EQ_U32(2U, LockLogCount);
  EXPECT_EQ_U32(APP_RESOURCE_UART, LockLog[0]);
  EXPECT_EQ_U32(APP_RESOURCE_FRAMEBUFFER, LockLog[1]);
  EXPECT_EQ_U32(2U, UnlockLogCount);
  EXPECT_EQ_U32(APP_RESOURCE_FRAMEBUFFER, UnlockLog[0]);
  EXPECT_EQ_U32(APP_RESOURCE_UART, UnlockLog[1]);
}

static void TestUartTransportRejectsInvalidFrame(void)
{
  ResetMocks();

  EXPECT_EQ_STATUS(APP_STATUS_INVALID_ARG,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS, 0U, 2U, 2U));
  EXPECT_EQ_STATUS(APP_STATUS_SECURITY_ERROR,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS - 1U, 4U, 2U, 2U));
  EXPECT_EQ_U32(0U, UartCallCount);
  EXPECT_EQ_U32(0U, LockLogCount);
  EXPECT_EQ_U32(0U, UnlockLogCount);
}

static void TestUartTransportChunksLargePayload(void)
{
  ResetMocks();
  FillFrameBuffer(APP_UART_TX_CHUNK_SIZE + 4U);

  EXPECT_EQ_STATUS(APP_STATUS_OK,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS, 2050U, 1U, 1U));

  EXPECT_EQ_U32(3U, UartCallCount);
  EXPECT_EQ_U32(SNAPSHOT_PROTOCOL_HEADER_SIZE, UartCalls[0].Size);
  EXPECT_EQ_U32(APP_UART_TX_CHUNK_SIZE, UartCalls[1].Size);
  EXPECT_EQ_U32(4U, UartCalls[2].Size);
  EXPECT_EQ_U32(APP_UART_TX_CHUNK_SIZE + 4U, LoadLe32(&UartCalls[0].Data[16]));
  EXPECT_EQ_U32(2U, LoadLe32(&UartCalls[0].Data[20]));
}

static void TestUartTransportReturnsResourceFailure(void)
{
  ResetMocks();
  LockResult = APP_STATUS_BUSY;
  LockFailResource = APP_RESOURCE_UART;

  EXPECT_EQ_STATUS(APP_STATUS_BUSY,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS, 4U, 2U, 2U));
  EXPECT_EQ_U32(0U, UartCallCount);
  EXPECT_EQ_U32(1U, LockLogCount);
  EXPECT_EQ_U32(0U, UnlockLogCount);

  ResetMocks();
  LockResult = APP_STATUS_BUSY;
  LockFailResource = APP_RESOURCE_FRAMEBUFFER;

  EXPECT_EQ_STATUS(APP_STATUS_BUSY,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS, 4U, 2U, 2U));
  EXPECT_EQ_U32(0U, UartCallCount);
  EXPECT_EQ_U32(2U, LockLogCount);
  EXPECT_EQ_U32(1U, UnlockLogCount);
  EXPECT_EQ_U32(APP_RESOURCE_UART, UnlockLog[0]);
}

static void TestUartTransportReturnsHalFailure(void)
{
  ResetMocks();
  FillFrameBuffer(16U);
  UartTransmitResult = HAL_ERROR;

  EXPECT_EQ_STATUS(APP_STATUS_ERROR,
                   AppUartTransport_SendSnapshot(BUFFER_ADDRESS, 4U, 2U, 2U));
  EXPECT_EQ_U32(1U, UartCallCount);
  EXPECT_EQ_U32(2U, UnlockLogCount);
  EXPECT_EQ_U32(APP_RESOURCE_FRAMEBUFFER, UnlockLog[0]);
  EXPECT_EQ_U32(APP_RESOURCE_UART, UnlockLog[1]);
}

static void RunTest(const TestCase_t *Test)
{
  CurrentTestName = Test->Name;
  CurrentTestFailed = 0;
  TestsRun++;

  Test->Function();

  if (CurrentTestFailed == 0)
  {
    TestsPassed++;
    printf("[PASS] %s\n", Test->Name);
  }
}

int main(void)
{
  static const TestCase_t tests[] = {
    {"snapshot header builds valid SNAP v2 frame", TestSnapshotHeaderValidFrame},
    {"snapshot header rejects invalid inputs", TestSnapshotHeaderRejectsInvalidInputs},
    {"memory payload size and framebuffer bounds", TestMemoryPayloadSizeAndBounds},
    {"memory d-cache invalidation aligns address range", TestMemoryDCacheInvalidationAlignment},
    {"crypto rejects invalid inputs", TestCryptoRejectsInvalidInputs},
    {"crypto matches AES-CTR known vector", TestCryptoKnownCtrVector},
    {"crypto chunking matches one-shot output", TestCryptoChunkingMatchesOneShot},
    {"uart transport rejects invalid frame", TestUartTransportRejectsInvalidFrame},
    {"uart transport sends encrypted snapshot", TestUartTransportSendsEncryptedSnapshot},
    {"uart transport chunks large payload", TestUartTransportChunksLargePayload},
    {"uart transport returns resource failure", TestUartTransportReturnsResourceFailure},
    {"uart transport returns HAL failure", TestUartTransportReturnsHalFailure},
  };
  uint32_t index;

  for (index = 0U; index < ARRAY_LEN(tests); index++)
  {
    RunTest(&tests[index]);
  }

  printf("\n%lu/%lu C driver unit tests passed\n",
         (unsigned long)TestsPassed,
         (unsigned long)TestsRun);

  return (TestsPassed == TestsRun) ? 0 : 1;
}
