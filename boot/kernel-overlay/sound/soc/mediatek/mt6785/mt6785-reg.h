/* SPDX-License-Identifier: GPL-2.0 */
/* MT6785 register subset, Copyright (c) 2018 MediaTek Inc.
 * From Xiaomi_Kernel_OpenSource, begonia-p-oss, mt6785-reg.h. */
#ifndef __MT6785_REG_H
#define __MT6785_REG_H

#define VUL12_ON_SFT                                   31
#define DL1_ON_SFT                                     4
#define AFE_ON_RETM_MASK_SFT                           (0x1 << 0)
#define MTKAIF_RXIF_CLKINV_ADC_MASK_SFT               (0x1U << 31)
#define MTKAIF_RXIF_DELAY_CYCLE_SFT                   12
#define MTKAIF_RXIF_DELAY_CYCLE_MASK_SFT              (0xf << 12)
#define MTKAIF_RXIF_DELAY_DATA_SFT                    8
#define MTKAIF_RXIF_DELAY_DATA_MASK_SFT               (0x1 << 8)
#define USE_3RD_SDM_MASK_SFT                           (0x1 << 28)
#define ATTGAIN_CTL_SFT                                0
#define ATTGAIN_CTL_MASK_SFT                           (0x3f << 0)
#define DL1_MODE_SFT                                   24
#define DL1_MODE_MASK                                  0xf
#define DL1_MONO_SFT                                   8
#define DL1_NORMAL_MODE_MASK_SFT                       (0x1 << 5)
#define DL1_HD_MODE_SFT                                0
#define VUL12_MODE_SFT                                 24
#define VUL12_MODE_MASK                                0xf
#define VUL12_4CH_EN_MASK_SFT                          (0x1 << 11)
#define VUL12_MONO_SFT                                 8
#define VUL12_NORMAL_MODE_MASK_SFT                     (0x1 << 5)
#define VUL12_HD_MODE_SFT                              0
#define IRQ1_MCU_MODE_SFT                              4
#define IRQ1_MCU_MODE_MASK                             0xf
#define IRQ0_MCU_MODE_SFT                              0
#define IRQ0_MCU_MODE_MASK                             0xf
#define ADDA_SDM_AUTO_RESET_ONOFF_MASK_SFT             (0x1U << 31)
#define AUDIO_TOP_CON0                            0x0000
#define AFE_DAC_CON0                              0x0010
#define AFE_CONN3                                 0x002c
#define AFE_CONN4                                 0x0030
#define AFE_CONN_24BIT                            0x0048
#define AFE_DL1_CON0                              0x004c
#define AFE_DL1_BASE_MSB                          0x0050
#define AFE_DL1_BASE                              0x0054
#define AFE_DL1_CUR_MSB                           0x0058
#define AFE_DL1_CUR                               0x005c
#define AFE_DL1_END_MSB                           0x0060
#define AFE_DL1_END                               0x0064
#define AFE_ADDA_DL_SRC2_CON0                     0x0108
#define AFE_ADDA_DL_SRC2_CON1                     0x010c
#define AFE_ADDA_UL_SRC_CON0                      0x0114
#define AFE_ADDA_TOP_CON0                         0x0120
#define AFE_ADDA_UL_DL_CON0                       0x0124
#define AFE_ADDA_PREDIS_CON0                      0x0260
#define AFE_ADDA_PREDIS_CON1                      0x0264
#define AFE_IRQ_MCU_CON1                          0x02e4
#define AFE_DAC_MON                               0x02ec
#define AFE_IRQ_MCU_CNT0                          0x0300
#define AFE_VUL12_CON0                            0x0374
#define AFE_VUL12_BASE_MSB                        0x0378
#define AFE_VUL12_BASE                            0x037c
#define AFE_VUL12_CUR_MSB                         0x0380
#define AFE_VUL12_CUR                             0x0384
#define AFE_VUL12_END_MSB                         0x0388
#define AFE_VUL12_END                             0x038c
#define AFE_IRQ_MCU_CON0                          0x03a0
#define AFE_IRQ_MCU_STATUS                        0x03a4
#define AFE_IRQ_MCU_CLR                           0x03a8
#define AFE_IRQ_MCU_CNT1                          0x03ac
#define AFE_IRQ_MCU_EN                            0x03b4
#define AFE_IRQ_MCU_SCP_EN                        0x03d4
#define AFE_CONN21                                0x0470
#define AFE_CONN22                                0x0474
#define AFE_CONN_24BIT_1                          0x0994
#define AFE_ADDA_DL_SDM_DCCOMP_CON                0x0c50
#define AFE_ADDA_DL_SDM_AUTO_RESET_CON            0x0c74
#define AFE_ADDA_MTKAIF_CFG0                      0x0e00
#define AFE_ADDA_MTKAIF_RX_CFG0                   0x0e20
#define AFE_ADDA_MTKAIF_RX_CFG2                   0x0e28
#define AFE_AUD_PAD_TOP                           0x0e40
#define AFE_IRQ_CNT_MASK 0x3ffff

#define DL2_ON_SFT                                     5
#define DL2_MODE_SFT                                   24
#define DL2_MODE_MASK                                  0xf
#define DL2_MONO_SFT                                   8
#define DL2_NORMAL_MODE_MASK_SFT                       (0x1 << 5)
#define DL2_HD_MODE_SFT                                0
#define IRQ2_MCU_MODE_SFT                              8
#define IRQ2_MCU_MODE_MASK                             0xf
#define AFE_CONN0                                 0x0020
#define AFE_CONN1                                 0x0024
#define AFE_I2S_CON3                              0x0040
#define AFE_DL2_CON0                              0x0068
#define AFE_DL2_BASE_MSB                          0x006c
#define AFE_DL2_BASE                              0x0070
#define AFE_DL2_CUR_MSB                           0x0074
#define AFE_DL2_CUR                               0x0078
#define AFE_DL2_END_MSB                           0x007c
#define AFE_DL2_END                               0x0080
#define AFE_IRQ_MCU_CNT2                          0x03b0
#define AFE_APLL2_TUNER_CFG                       0x03f4
#define AFE_HD_ENGEN_ENABLE                       0x0dd0
#endif
