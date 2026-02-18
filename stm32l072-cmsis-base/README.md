# STM32L072 CMSIS Base

Projet CMSIS-Toolbox minimal pour `STM32L072RBTx`, avec startup et linker locaux (pas de dependance a CubeMX).

## Contenu

- `stm32l072-base.csolution.yml`: solution CMSIS (`Debug` / `Release`)
- `stm32l072-base.cproject.yml`: projet CMSIS (CMSIS-CORE + sources locaux)
- `src/main.c`: application minimale
- `src/system_stm32l0xx.c`: stubs `SystemInit` et `SystemCoreClockUpdate`
- `startup/startup_stm32l072xx.s`: vector table + `Reset_Handler`
- `linker/stm32l072rb_flash.ld`: script linker FLASH/RAM

## Build local (VS Code / terminal)

Prerequis:
- `csolution`, `cbuild`, `cpackget` (CMSIS-Toolbox)
- Toolchain GCC ARM exportee dans `GCC_TOOLCHAIN_10_3_1`

Exemple:

```bash
export PATH="$HOME/.local/bin:$PATH"
export GCC_TOOLCHAIN_10_3_1="$HOME/.local/opt/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin"
cd stm32l072-cmsis-base

cpackget add -a ARM::CMSIS Keil::STM32L0xx_DFP
csolution convert stm32l072-base.csolution.yml -l required -t GCC -c .Debug+STM32L072RB
cbuild stm32l072-base.csolution.yml -c .Debug+STM32L072RB --toolchain GCC --update-rte
cbuild stm32l072-base.csolution.yml -c .Release+STM32L072RB --toolchain GCC --update-rte
```
