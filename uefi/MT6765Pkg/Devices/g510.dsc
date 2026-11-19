[Defines]
  PLATFORM_NAME                  = MT6765Pkg
  PLATFORM_GUID                  = 28f1a3bf-193a-47e3-a7b9-5a435eaab2ee
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MT6765Pkg/MT6765Pkg.fdf

!include MT6765Pkg/MT6765Pkg.dsc

#
# General Mobile GM 21 Plus (G510), MT6765, 720x1600, 4 GB DRAM.
#
# Display values from LK's atag,videolfb on G510:
#   fb_base = 0x7E250000, vram = 0x00FB0000
# Panel: 720x1600, 32bpp, stride 736 (2944 bytes)
# Touchscreen: Novatek NT36525 on SPI0 (not on-screen buttons)
# Hardware keys: Power/Volume via keypad at 0x10010000
#
[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0xC0000000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x40C00000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000
  gMT6765PkgTokenSpaceGuid.PcdUefiMemPoolBase|0x40D00000
  gMT6765PkgTokenSpaceGuid.PcdUefiMemPoolSize|0x03300000
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x40C40000

  # Keep debug minimal to avoid framebuffer flush overhead
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000000

  # G510 framebuffer from LK's atag,videolfb
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferAddress|0x7E250000
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferWidth|720
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferHeight|1600
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferVisibleWidth|720
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferVisibleHeight|1600
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferPixelBpp|32
  gMT6765PkgTokenSpaceGuid.PcdMipiFrameBufferStride|736

  # GICv3 on MT6765
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x0C000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0C100000

  # Generic timer at 13 MHz, PPIs from DTB: <1 13> <1 14> <1 11> <1 10>
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|13000000

[BuildOptions]
  # Selects G510 descriptor table in Include/Configuration/DeviceMemoryMap.h
  GCC:*_*_AARCH64_CC_FLAGS = -DMT6765_DEVICE_G510
  GCC:*_*_AARCH64_DLINK_FLAGS = -Wl,-u,memcpy -Wl,-u,memset
