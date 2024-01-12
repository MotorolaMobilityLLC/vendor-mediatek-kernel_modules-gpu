// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

/* RGX */
#include "interrupt_support.h"
#include "pvrsrv_device.h"
#include "rgxdevice.h"
#include "syscommon.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif
/* MTK */
#include "mtk_mfgsys.h"

#define RGX_CR_ISP_GRIDOFFSET   (0x0FA0U)

static RGX_TIMING_INFORMATION   gsRGXTimingInfo;
static RGX_DATA                 gsRGXData;
static PVRSRV_DEVICE_CONFIG     gsDevices[1];
static PHYS_HEAP_FUNCTIONS      gsPhysHeapFuncs;
static PHYS_HEAP_CONFIG         gsPhysHeapConfig;

static struct platform_device   *gpsPVRCfgDev;

/* CPU to Device physcial address translation */
static void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				   IMG_UINT32 ui32NumOfAddr,
				   IMG_DEV_PHYADDR *psDevPAddr,
				   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;

	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;

		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
	}
}

/* Device to CPU physcial address translation */
static void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				   IMG_UINT32 ui32NumOfAddr,
				   IMG_CPU_PHYADDR *psCpuPAddr,
				   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;

		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
	}
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_ERROR err = PVRSRV_OK;
	struct resource *irq_res, *reg_res;

	gsPhysHeapFuncs.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr;
	gsPhysHeapFuncs.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr;

	gsPhysHeapConfig.pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig.eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig.psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig.hPrivData = (IMG_HANDLE)&gsDevices[0];
	// [TIM] hack
	gsPhysHeapConfig.ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL;

	gsDevices[0].pvOSDevice = pvOSDevice;
	gsDevices[0].pasPhysHeaps = &gsPhysHeapConfig;

	gsDevices[0].ui32PhysHeapCount =
			sizeof(gsPhysHeapConfig) / sizeof(PHYS_HEAP_CONFIG);

	/* Setup RGX specific timing data */
	gsRGXTimingInfo.ui32CoreClockSpeed = RGX_HW_CORE_CLOCK_SPEED;

	gsRGXTimingInfo.bEnableActivePM = true;
	gsRGXTimingInfo.ui32ActivePMLatencyms = SYS_RGX_ACTIVE_POWER_LATENCY_MS,

	/* define HW APM */
#if defined(MTK_USE_HW_APM)
	gsRGXTimingInfo.bEnableRDPowIsland = true;
#else
	gsRGXTimingInfo.bEnableRDPowIsland = false;
#endif

	/* Setup RGX specific data */
	gsRGXData.psRGXTimingInfo = &gsRGXTimingInfo;

	/* Setup RGX device */
	gsDevices[0].pszName = "RGX";
	gsDevices[0].pszVersion = NULL;

	gpsPVRCfgDev = to_platform_device((struct device *)pvOSDevice);
	irq_res = platform_get_resource(gpsPVRCfgDev, IORESOURCE_IRQ, 0);

	if (irq_res) {
		gsDevices[0].ui32IRQ = irq_res->start;
		PVR_LOG(("irq_res = 0x%x", (int)irq_res->start));
	} else {
		PVR_DPF((PVR_DBG_ERROR, "irq_res = NULL!"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	reg_res = platform_get_resource(gpsPVRCfgDev, IORESOURCE_MEM, 0);

	if (reg_res) {
		gsDevices[0].sRegsCpuPBase.uiAddr = reg_res->start;
		gsDevices[0].ui32RegsSize = resource_size(reg_res);

		PVR_LOG(("reg_res = 0x%x, 0x%x",
				(int)reg_res->start,
				(int)resource_size(reg_res)));
	} else {
		PVR_DPF((PVR_DBG_ERROR, "reg_res = NULL!"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

#if defined(SUPPORT_ALT_REGBASE)
	gsDevices[0].sAltRegsGpuPBase.uiAddr = 0x7F000000;
#endif

	/* Power management on HW system */
	gsDevices[0].pfnPrePowerState = MTKSysPrePowerState;
	gsDevices[0].pfnPostPowerState = MTKSysPostPowerState;

	/* Clock frequency */
	gsDevices[0].pfnClockFreqGet = NULL;

	gsDevices[0].hDevData = &gsRGXData;
	gsDevices[0].eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE;

#if defined(CONFIG_MACH_MT6855)
	gsDevices[0].bHasFBCDCVersion31 = IMG_TRUE;
#else
	gsDevices[0].bHasFBCDCVersion31 = IMG_FALSE;
#endif

	/* Setup other system specific stuff */
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	gsDevices[0].pvOSDevice = pvOSDevice;
	*ppsDevConfig = &gsDevices[0];

	err = MTKRGXDeviceInit(gsDevices);
	return err;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	MTKRGXDeviceDeInit(gsDevices);
	psDevConfig->pvOSDevice = NULL;
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
				  IMG_UINT32 ui32IRQ,
				  const IMG_CHAR *pszName,
				  PFN_LISR pfnLISR,
				  void *pvData,
				  IMG_HANDLE *phLISRData)
{
	IMG_UINT32 ui32IRQFlags = SYS_IRQ_FLAG_TRIGGER_LOW;

	PVR_UNREFERENCED_PARAMETER(hSysData);

#if defined(PVRSRV_GPUVIRT_MULTIDRV_MODEL)
	ui32IRQFlags |= SYS_IRQ_FLAG_SHARED;
#endif

	return OSInstallSystemLISR(phLISRData, ui32IRQ,
				   pszName, pfnLISR,
				   pvData,  ui32IRQFlags);
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	return OSUninstallSystemLISR(hLISRData);
}


PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
	DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf, void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	return PVRSRV_OK;
}
