# SPDX-License-Identifier: GPL-2.0
subdir-y += actions
subdir-y += airoha
subdir-y += allwinner
subdir-y += alphascale
subdir-y += amazon
subdir-y += amlogic
subdir-y += arm
subdir-y += aspeed
subdir-y += axis
subdir-y += broadcom
subdir-y += calxeda
subdir-y += cirrus
subdir-y += cnxt
subdir-y += gemini
subdir-y += hisilicon
#subdir-y += hpe
dtb-$(CONFIG_ARCH_HPE_GXP_GEN11) += gxp.dtb
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0236.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0235.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0241.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0243.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0246.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0248.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0249.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x024a.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0250.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0261.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0263.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0264.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0271.dtbo
dtbo-$(CONFIG_ARCH_HPE_GXP_GEN11) += proliant_0x0273.dtbo

dtb-$(CONFIG_ARCH_HPE_GXP_GEN11) += gxp_proliant_0x0235.dtb \
	gxp_proliant_0x0236.dtb \
    gxp_proliant_0x0243.dtb \
    gxp_proliant_0x0244.dtb \
    gxp_proliant_0x0245.dtb \
    gxp_proliant_0x0246.dtb \
    gxp_proliant_0x0248.dtb \
    gxp_proliant_0x0249.dtb \
    gxp_proliant_0x0249.dtb \
    gxp_proliant_0x0261.dtb \
    gxp_proliant_0x0263.dtb \
    gxp_proliant_0x0264.dtb \
    gxp_proliant_0x0271.dtb \
    gxp_proliant_0x0273.dtb

arch/arm/boot/dts/gxp_proliant_%.dtb: \
	arch/arm/boot/dts/gxp.dtb \
	arch/arm/boot/dts/proliant_%.dtbo
	@echo "  FDOVERLAY $@"
	$(Q)fdtoverlay -i $< -o $@ $(word 2,$^)
	
#dtb-$(CONFIG_ARCH_HPE_GXP_GEN11) += gxp_proliant_0x0236.dtb
subdir-y += intel
subdir-y += marvell
subdir-y += mediatek
subdir-y += microchip
subdir-y += moxa
subdir-y += nspire
subdir-y += nuvoton
subdir-y += nvidia
subdir-y += nxp
subdir-y += qcom
subdir-y += realtek
subdir-y += renesas
subdir-y += rockchip
subdir-y += samsung
subdir-y += sigmastar
subdir-y += socionext
subdir-y += st
subdir-y += sunplus
subdir-y += synaptics
subdir-y += ti
subdir-y += unisoc
subdir-y += vt8500
subdir-y += xen
subdir-y += xilinx

