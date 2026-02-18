# STM32L072 CMSIS Base (Keil Studio Cloud)

Projet de base CMSIS-Toolbox pour `STM32L072RBTx`.

## Contenu

- `stm32l072-base.csolution.yml`: solution CMSIS
- `stm32l072-base.cproject.yml`: projet CMSIS
- `src/main.c`: point d'entree minimal

## Import dans Keil Studio Cloud

1. Ouvrir le repo dans Keil Studio Cloud.
2. Ouvrir `stm32l072-cmsis-base/stm32l072-base.csolution.yml`.
3. Laisser Keil installer les packs demandes si necessaire (`ARM::CMSIS` et `Keil::STM32L0xx_DFP`).
4. Compiler le contexte `+STM32L072RB`.
