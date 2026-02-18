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
4. Installer les packs demandes.
5. Ouvrir la vue CMSIS Software Components et lancer le generateur `Device:CubeMX`.
6. Compiler le contexte `Debug+STM32L072RB` (ou `Release+STM32L072RB`).
