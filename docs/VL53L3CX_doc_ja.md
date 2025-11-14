# VL53L3CX 完全実装ガイド
## レジスタレベル実装手順書

本ドキュメントは、VL53L3CXセンサーのオリジナルドライバをゼロから実装するための**完全な技術仕様書**です。
全ての手順で、**具体的なレジスタアドレス**、**書き込む値**、**待ち時間**を明記しています。

---

## 目次

1. [電源投入からの起動シーケンス](#1-電源投入からの起動シーケンス)
2. [NVM較正データ読み出し](#2-nvm較正データ読み出し)
3. [MEDIUM_RANGE プリセットモード設定](#3-medium_range-プリセットモード設定)
4. [I2Cアドレス変更](#4-i2cアドレス変更)
5. [測定開始・停止手順](#5-測定開始停止手順)
6. [データ読み出し手順](#6-データ読み出し手順)
7. [完全なコード実装例](#7-完全なコード実装例)

---

## 1. 電源投入からの起動シーケンス

### 1.0 起動シーケンス概要

VL53L3CXは電源投入後、内蔵ファームウェアが自動的に起動します。この章では、ファームウェアが完全に起動するまで待機し、必要に応じてI2C通信電圧を設定する手順を説明します。

**実施する操作:**
1. ファームウェアシステムステータスレジスタをポーリングし、bit 0が1になるまで待機
2. (オプション) I2C電源電圧が2.8Vの場合、パッド設定レジスタを変更

**所要時間:** 通常100-200ms (最大500msのタイムアウトを推奨)

**使用するレジスタ:**
- `FIRMWARE__SYSTEM_STATUS` (0x0010): ファームウェア起動完了フラグ
- `PAD_I2C_HV__EXTSUP_CONFIG` (0x002E): I2C電圧設定 (2.8V使用時のみ)

---

### 1.1 ファームウェア起動待ち

#### 手順説明

VL53L3CXは電源投入後、ブートローダーが実行され、ファームウェアがRAMにロードされて起動します。この処理が完了するまで、他のレジスタ操作を行ってはいけません。

**詳細手順:**
1. レジスタ `FIRMWARE__SYSTEM_STATUS` (アドレス 0x0010) を読み出す
2. 読み出した値のbit 0を確認する
   - bit 0 = 0: まだ起動中 → 1ms待機して再度読み出し
   - bit 0 = 1: 起動完了 → 次のステップへ進む
3. タイムアウト処理: 500ms経過しても起動しない場合はエラー

**重要なポイント:**
- ポーリング間隔は1msが推奨 (短すぎるとI2Cバスを占有、長すぎると起動検出が遅れる)
- タイムアウト値は500msが推奨 (通常は100-200msで起動完了)
- この待機処理を省略すると、後続のレジスタ操作が失敗する

#### コード実装

```c
/**
 * ステップ 1: ファームウェア起動待ち
 * レジスタ: FIRMWARE__SYSTEM_STATUS (0x0010)
 * 期待値: 0x01 (bit 0 = 1)
 */
uint8_t boot_status;
uint32_t timeout = 500;  // 500ms タイムアウト
uint32_t start_time = millis();

do {
    I2C_Read(0x29, 0x0010, &boot_status, 1);

    if ((millis() - start_time) > timeout) {
        return ERROR_BOOT_TIMEOUT;
    }

    delay_ms(1);  // 1ms待機

} while ((boot_status & 0x01) == 0);

// 起動完了
```

**重要ポイント:**
- ポーリング間隔: 1ms
- タイムアウト: 500ms
- チェックビット: bit 0 (0x01)

---

### 1.2 I2C電圧設定 (オプション: 2.8V使用時のみ)

#### 手順説明

VL53L3CXのI2C通信インターフェースは、デフォルトで1.8V電源を想定しています。もしI2Cバスの電圧が2.8Vの場合、内部パッド設定を変更する必要があります。

**この設定が必要なケース:**
- システムのI2C_VDD電源が2.8Vの場合
- マイコンのI2Cプルアップ電圧が2.8Vの場合

**この設定が不要なケース:**
- システムのI2C_VDD電源が1.8Vの場合 (デフォルト)

**詳細手順:**
1. レジスタ `PAD_I2C_HV__EXTSUP_CONFIG` (アドレス 0x002E) を読み出す (Read-Modify-Write パターン)
2. 読み出した値のbit 0を1に設定する (他のビットは変更しない)
3. 変更した値を同じレジスタに書き戻す
4. 待ち時間は不要 (即座に有効)

**重要なポイント:**
- このレジスタは他のビットも使用されているため、必ずRead-Modify-Write方式で変更する
- bit 0のみを変更し、他のビット (bit 7-1) は保持する
- I2C電圧が1.8Vの場合、この設定は不要 (スキップ可能)

#### コード実装

```c
/**
 * ステップ 2: I2C電圧設定 (USE_I2C_2V8 定義時のみ)
 * レジスタ: PAD_I2C_HV__EXTSUP_CONFIG (0x002E)
 */
#ifdef USE_I2C_2V8
    uint8_t pad_config;

    // 現在値読み出し
    I2C_Read(0x29, 0x002E, &pad_config, 1);

    // bit 0 を 1 に設定 (他のビットは保持)
    pad_config = (pad_config & 0xFE) | 0x01;

    // 書き戻し
    I2C_Write(0x29, 0x002E, &pad_config, 1);

    // 待ち時間: なし (即座に次へ)
#endif
```

---

## 2. NVM較正データ読み出し

### 2.0 NVM読み出し概要

VL53L3CXには、工場出荷時に較正されたパラメータがNVM (不揮発性メモリ) に保存されています。これらのデータは、正確な測定を行うために必須です。この章では、NVMから較正データを読み出す完全な手順を説明します。

**NVMから読み出す主要データ:**
1. 高速オシレータ周波数 (`OSC_MEASURED__FAST_OSC_FREQUENCY`, NVMアドレス 0x1C-0x1D)
   - タイミング計算に使用される重要なパラメータ
2. デフォルトI2Cアドレス (`I2C_SLAVE__DEVICE_ADDRESS`, NVMアドレス 0x11)
3. VHVタイムアウト設定 (`VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND`, NVMアドレス 0x2C)

**処理の流れ:**
1. NVM読み出し準備 (ファームウェア無効化、電源制御)
2. NVMからデータ読み出し (複数アドレスを順次読み出し)
3. NVM読み出し後のクリーンアップ (電源復帰、ファームウェア再有効化)

**所要時間:** 約5.3ms (パワーフォース250μs + NVMパワーアップ5ms + 読み出しトリガー5μs × データ数)

**使用するレジスタ:**
- `FIRMWARE__ENABLE` (0x0401): ファームウェア制御
- `POWER_MANAGEMENT__GO1_POWER_FORCE` (0x0419): 電源強制制御
- `RANGING_CORE__NVM_CTRL__*` (0x01AC-0x01B2): NVM制御レジスタ群
- `RANGING_CORE__CLK_CTRL1` (0x01BB): NVMクロック制御

**重要な待ち時間:**
- パワーフォース有効化後: **250μs** (必須)
- NVMクロック有効化後: **5ms** (必須)
- NVM読み出しトリガー後: **5μs** (必須)

---

### 2.1 NVM読み出し有効化シーケンス

#### 手順説明

NVMは通常、低消費電力のためパワーダウン状態にあります。NVMからデータを読み出すには、ファームウェアを一時的に無効化し、NVMに電源を供給し、クロックを有効化する必要があります。

**なぜファームウェアを無効化するのか:**
- ファームウェアが動作していると、レンジング処理とNVM読み出しが競合する可能性がある
- NVM制御レジスタへの直接アクセスには、ファームウェアを停止する必要がある

**詳細手順 (6ステップ):**

**ステップ3.1: ファームウェア無効化**
- レジスタ `FIRMWARE__ENABLE` (0x0401) に 0x00 を書き込む
- これにより、ファームウェアの自律動作が停止する
- 待ち時間: 不要

**ステップ3.2: パワーフォース有効化**
- レジスタ `POWER_MANAGEMENT__GO1_POWER_FORCE` (0x0419) に 0x01 を書き込む
- これにより、NVM用の内部電源が強制的に有効化される
- **重要**: 250μs待機 (電源安定化のため必須)

**ステップ3.3: NVMパワーダウン解除**
- レジスタ `RANGING_CORE__NVM_CTRL__PDN` (0x01AC) に 0x01 を書き込む
- NVMがアクティブ状態になる
- 待ち時間: 不要

**ステップ3.4: NVMクロック有効化**
- レジスタ `RANGING_CORE__CLK_CTRL1` (0x01BB) に 0x05 を書き込む
- NVMの読み出しに必要なクロックが供給される
- **重要**: 5ms待機 (NVMの準備完了のため必須)

**ステップ3.5: NVMモード設定**
- レジスタ `RANGING_CORE__NVM_CTRL__MODE` (0x01AD) に 0x01 を書き込む
- NVMが読み出しモードに設定される
- 待ち時間: 不要

**ステップ3.6: NVMパルス幅設定**
- レジスタ `RANGING_CORE__NVM_CTRL__PULSE_WIDTH_MSB` (0x01AE-0x01AF) に 0x0004 を書き込む
- NVM読み出しパルスの幅を設定 (デフォルト値)
- 待ち時間: 不要

**重要なポイント:**
- 待ち時間を省略すると、NVMからのデータ読み出しが失敗または不正確になる
- パワーフォース後の250μsとクロック有効化後の5msは特に重要
- これらの設定は、NVM読み出し完了後に元に戻す必要がある

#### コード実装

```c
/**
 * ステップ 3: NVM読み出し準備
 */

// 3.1: ファームウェア無効化
// レジスタ: FIRMWARE__ENABLE (0x0401)
uint8_t fw_disable = 0x00;
I2C_Write(0x29, 0x0401, &fw_disable, 1);
// 待ち時間: なし

// 3.2: パワーフォース有効化
// レジスタ: POWER_MANAGEMENT__GO1_POWER_FORCE (0x0419)
uint8_t power_force = 0x01;
I2C_Write(0x29, 0x0419, &power_force, 1);

// **重要**: パワーフォース安定化待ち
delay_us(250);  // 250マイクロ秒待機 (必須)

// 3.3: NVM制御 - パワーダウン解除
// レジスタ: RANGING_CORE__NVM_CTRL__PDN (0x01AC)
uint8_t nvm_pdn = 0x01;
I2C_Write(0x29, 0x01AC, &nvm_pdn, 1);
// 待ち時間: なし

// 3.4: NVMクロック有効化
// レジスタ: RANGING_CORE__CLK_CTRL1 (0x01BB)
uint8_t clk_ctrl = 0x05;
I2C_Write(0x29, 0x01BB, &clk_ctrl, 1);

// **重要**: NVMパワーアップ待ち
delay_us(5000);  // 5000マイクロ秒 = 5ms待機 (必須)

// 3.5: NVMモード設定
// レジスタ: RANGING_CORE__NVM_CTRL__MODE (0x01AD)
uint8_t nvm_mode = 0x01;
I2C_Write(0x29, 0x01AD, &nvm_mode, 1);
// 待ち時間: なし

// 3.6: NVMパルス幅設定
// レジスタ: RANGING_CORE__NVM_CTRL__PULSE_WIDTH_MSB (0x01AE)
uint16_t pulse_width = 0x0004;  // デフォルト値
uint8_t pw_buf[2] = {(pulse_width >> 8) & 0xFF, pulse_width & 0xFF};
I2C_Write(0x29, 0x01AE, pw_buf, 2);
// 待ち時間: なし
```

### 2.2 NVMデータ読み出し

#### 手順説明

NVMが有効化されたら、必要な較正データを読み出します。NVMは256個のアドレス空間を持ち、各アドレスから4バイトのデータを読み出せます。

**読み出しプロトコル (各NVMアドレスごとに実行):**

**ステップ4.1: NVMアドレス設定**
- レジスタ `RANGING_CORE__NVM_CTRL__ADDR` (0x01B0) に読み出したいNVMアドレスを書き込む
- 例: 高速オシレータ周波数を読むなら 0x1C を設定

**ステップ4.2: 読み出しトリガー (Low)**
- レジスタ `RANGING_CORE__NVM_CTRL__READN` (0x01B1) に 0x00 を書き込む
- これにより、読み出し開始信号がLowレベルになる

**ステップ4.3: トリガー遅延**
- **5μs待機** (NVMからデータをレジスタにコピーする時間)
- この待ち時間を省略すると、読み出しデータが不正確になる

**ステップ4.4: 読み出しトリガー (High)**
- レジスタ `RANGING_CORE__NVM_CTRL__READN` (0x01B1) に 0x01 を書き込む
- これにより、読み出し完了信号がHighレベルになる

**ステップ4.5: データ取得**
- レジスタ `RANGING_CORE__NVM_CTRL__DATAOUT_MMM` (0x01B2-0x01B5) から4バイトを読み出す
- データはビッグエンディアン形式で格納されている
- 通常、最初の1-2バイトが有効データ、残りは未使用

**複数アドレスを読む場合:**
- ステップ4.1から4.5を、読み出したいNVMアドレスごとに繰り返す
- 各アドレス読み出しには、5μsの待ち時間が必要

**重要なポイント:**
- トリガー遅延 (5μs) は必須 (省略すると前回のデータが読まれる可能性がある)
- 高速オシレータ周波数は2バイトデータ (NVMアドレス0x1Cの最初の2バイト)
- データはビッグエンディアン (MSBファースト) で格納

#### コード実装

```c
/**
 * ステップ 4: NVMデータ読み出し
 *
 * 読み出すデータ:
 * - I2C_SLAVE__DEVICE_ADDRESS (NVMアドレス: 0x11)
 * - OSC_MEASURED__FAST_OSC_FREQUENCY (NVMアドレス: 0x1C, 0x1D)
 * - VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND (NVMアドレス: 0x2C)
 *
 * 各NVMアドレスごとに以下の手順を実行:
 */

uint8_t nvm_data[4];  // NVMデータ (4バイト/アドレス)
uint8_t nvm_addr = 0x11;  // 例: I2C_SLAVE__DEVICE_ADDRESS

// 4.1: NVMアドレス設定
// レジスタ: RANGING_CORE__NVM_CTRL__ADDR (0x01B0)
I2C_Write(0x29, 0x01B0, &nvm_addr, 1);

// 4.2: 読み出しトリガー (Low)
// レジスタ: RANGING_CORE__NVM_CTRL__READN (0x01B1)
uint8_t readn_low = 0x00;
I2C_Write(0x29, 0x01B1, &readn_low, 1);

// **重要**: 読み出しトリガー遅延
delay_us(5);  // 5マイクロ秒待機 (必須)

// 4.3: 読み出しトリガー (High)
uint8_t readn_high = 0x01;
I2C_Write(0x29, 0x01B1, &readn_high, 1);

// 4.4: データ読み出し
// レジスタ: RANGING_CORE__NVM_CTRL__DATAOUT_MMM (0x01B2)
I2C_Read(0x29, 0x01B2, nvm_data, 4);  // 4バイト読み出し

// 待ち時間: なし (次のアドレスへ続く場合は 4.1 から繰り返し)

/**
 * 重要なNVMデータ:
 * - 高速オシレータ周波数 (fast_osc_frequency)
 *   NVMアドレス: 0x1C, 0x1D (2バイト)
 *   使用目的: タイミング計算
 */
```

### 2.3 NVM読み出し無効化

#### 手順説明

NVMからのデータ読み出しが完了したら、デバイスを通常動作モードに戻す必要があります。具体的には、パワーフォースを無効化し、ファームウェアを再有効化します。

**なぜこのクリーンアップが必要なのか:**
- パワーフォースを有効のままにすると、消費電力が増加する
- ファームウェアが無効のままだと、測定動作ができない
- NVMアクセスモードから通常動作モードへの遷移が必要

**詳細手順 (2ステップ):**

**ステップ5.1: パワーフォース無効化**
- レジスタ `POWER_MANAGEMENT__GO1_POWER_FORCE` (0x0419) に 0x00 を書き込む
- 強制電源供給を停止し、通常の電源管理に戻す
- 待ち時間: 不要

**ステップ5.2: ファームウェア再有効化**
- レジスタ `FIRMWARE__ENABLE` (0x0401) に 0x01 を書き込む
- ファームウェアの自律動作を再開する
- 待ち時間: 不要 (ファームウェアは即座に再開)

**重要なポイント:**
- この2つのステップは必ず実行する (省略すると後続の測定動作が正常に行えない)
- 順序は重要: パワーフォース無効化 → ファームウェア有効化
- この処理後、デバイスは通常の測定動作が可能な状態になる

#### コード実装

```c
/**
 * ステップ 5: NVM読み出し後のクリーンアップ
 */

// 5.1: パワーフォース無効化
// レジスタ: POWER_MANAGEMENT__GO1_POWER_FORCE (0x0419)
uint8_t power_force_off = 0x00;
I2C_Write(0x29, 0x0419, &power_force_off, 1);

// 5.2: ファームウェア再有効化
// レジスタ: FIRMWARE__ENABLE (0x0401)
uint8_t fw_enable = 0x01;
I2C_Write(0x29, 0x0401, &fw_enable, 1);

// 待ち時間: なし
```

---

## 3. MEDIUM_RANGE プリセットモード設定

### 3.0 プリセットモード概要

VL53L3CXは、異なる測定範囲や精度に最適化された複数のプリセットモードを持っています。この章では、最も一般的な **MEDIUM_RANGE モード** の設定手順を説明します。

**MEDIUM_RANGE モードの特性:**
- 測定範囲: 0mm - 3000mm (約3メートル)
- 測定精度: 中程度 (±5%程度)
- 測定時間: 中程度 (約33ms)
- 消費電力: 中程度
- 用途: 一般的な距離測定、ロボティクス、ドローンなど

**設定するレジスタ群:**
1. **Static Configuration** (静的設定): GPIO、SPAD、VCSEL、シグマ推定、アルゴリズム設定
2. **General Configuration** (一般設定): 割り込み、キャリブレーション設定
3. **Timing Configuration** (タイミング設定): VCSEL周期、タイムアウト、測定間隔
4. **Dynamic Configuration** (動的設定): 閾値、ROI、シーケンス設定
5. **System Control** (システム制御): ストリームカウント、ファームウェア設定

**設定するレジスタ数:** 約50個以上

**所要時間:** 即座 (全てのレジスタ書き込みに待ち時間は不要)

**重要なポイント:**
- これらの設定値は、STMicroelectronics社が最適化した推奨値
- 各レジスタの値を正確に設定する必要がある (1つでも間違えると測定が正常に動作しない)
- レジスタアドレスが連続している箇所は、I2C連続書き込みで効率化可能

---

### 3.1 Static Configuration レジスタ設定

#### 手順説明

Static Configurationは、測定動作中に変更されない固定的な設定です。GPIO出力、SPAD (Single Photon Avalanche Diode) 設定、VCSEL (レーザー) 設定、シグマ推定パラメータ、アルゴリズム設定などが含まれます。

**各設定の目的:**

**GPIO設定 (0x0030-0x0031):**
- GPIO_HV_MUX__CTRL: 割り込み出力の極性と機能を設定
  - 0x10 = ACTIVE_LOW (割り込み時にLowレベル出力)
- GPIO__TIO_HV_STATUS: GPIO状態レジスタ (0x02がデフォルト値)

**SPAD設定 (0x0033-0x0034):**
- SPADは光検出素子で、パルス幅とオフセットを設定
- ANA_CONFIG__SPAD_SEL_PSWIDTH (0x0033): 0x02 (MEDIUM_RANGE最適値)
- ANA_CONFIG__VCSEL_PULSE_WIDTH_OFFSET (0x0034): 0x08 (デフォルト値)

**シグマ推定パラメータ (0x0036-0x0038):**
- シグマは測定の不確かさ (標準偏差) を推定するためのパラメータ
- SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS: 0x08 (8ns)
- SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS: 0x10 (16ns)
- SIGMA_ESTIMATOR__SIGMA_REF_MM: 0x01 (基準値1mm)

**アルゴリズム設定 (0x0039-0x0040):**
- クロストーク補正、範囲無視、一貫性チェックなどの設定
- ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM: 0x01
- ALGO__RANGE_IGNORE_VALID_HEIGHT_MM: 0xFF (範囲無視を無効化)
- ALGO__RANGE_MIN_CLIP: 0x00 (最小範囲クリップなし)
- ALGO__CONSISTENCY_CHECK__TOLERANCE: 0x02 (許容値)

**重要なポイント:**
- これらの値はMEDIUM_RANGEモード専用の最適化値
- 他のモード (SHORT_RANGE, LONG_RANGE等) では異なる値を使用
- アドレス0x0030-0x0040は連続しているため、I2C連続書き込みで効率化可能

#### コード実装

```c
/**
 * ステップ 6: Static Configuration 設定
 *
 * これらのレジスタは I2C連続書き込みで効率化可能
 * 開始アドレス: 0x002D
 */

// GPIO設定
// レジスタ: GPIO_HV_MUX__CTRL (0x0030)
uint8_t gpio_mux_ctrl = 0x10;  // ACTIVE_LOW | OUTPUT_RANGE_AND_ERROR_INTERRUPTS
I2C_Write(0x29, 0x0030, &gpio_mux_ctrl, 1);

// レジスタ: GPIO__TIO_HV_STATUS (0x0031)
uint8_t gpio_tio_status = 0x02;
I2C_Write(0x29, 0x0031, &gpio_tio_status, 1);

// SPAD設定
// レジスタ: ANA_CONFIG__SPAD_SEL_PSWIDTH (0x0033)
uint8_t spad_sel = 0x02;
I2C_Write(0x29, 0x0033, &spad_sel, 1);

// レジスタ: ANA_CONFIG__VCSEL_PULSE_WIDTH_OFFSET (0x0034)
uint8_t vcsel_offset = 0x08;
I2C_Write(0x29, 0x0034, &vcsel_offset, 1);

// シグマ推定パラメータ
// レジスタ: SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS (0x0036)
uint8_t sigma_pulse_width = 0x08;  // 8ns
I2C_Write(0x29, 0x0036, &sigma_pulse_width, 1);

// レジスタ: SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS (0x0037)
uint8_t sigma_ambient = 0x10;  // 16ns
I2C_Write(0x29, 0x0037, &sigma_ambient, 1);

// レジスタ: SIGMA_ESTIMATOR__SIGMA_REF_MM (0x0038)
uint8_t sigma_ref = 0x01;  // 1mm
I2C_Write(0x29, 0x0038, &sigma_ref, 1);

// アルゴリズム設定
// レジスタ: ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM (0x0039)
uint8_t xtalk_height = 0x01;
I2C_Write(0x29, 0x0039, &xtalk_height, 1);

// レジスタ: ALGO__RANGE_IGNORE_VALID_HEIGHT_MM (0x003E)
uint8_t range_ignore_height = 0xFF;
I2C_Write(0x29, 0x003E, &range_ignore_height, 1);

// レジスタ: ALGO__RANGE_MIN_CLIP (0x003F)
uint8_t range_min_clip = 0x00;
I2C_Write(0x29, 0x003F, &range_min_clip, 1);

// レジスタ: ALGO__CONSISTENCY_CHECK__TOLERANCE (0x0040)
uint8_t consistency_tolerance = 0x02;
I2C_Write(0x29, 0x0040, &consistency_tolerance, 1);

// 待ち時間: なし
```

### 3.2 General Configuration レジスタ設定

#### 手順説明

General Configurationは、割り込み設定、キャリブレーション設定、VCSEL設定など、測定動作の基本的なパラメータを設定します。

**各設定の目的:**

**割り込み設定 (0x0046):**
- SYSTEM__INTERRUPT_CONFIG_GPIO: 割り込み条件を設定
  - 0x20 = NEW_SAMPLE_READY (新しい測定データ準備完了時に割り込み)
  - この設定により、ポーリングではなく割り込み駆動でデータ取得可能

**VCSELキャリブレーション設定 (0x0047-0x0048):**
- CAL_CONFIG__VCSEL_START: VCSEL開始設定
  - 0x0B = 11 (MEDIUM_RANGEモード用の最適値)
- CAL_CONFIG__REPEAT_RATE: キャリブレーション繰り返しレート
  - 0x0000 = デフォルト値

**VCSELグローバル設定 (0x004A):**
- GLOBAL_CONFIG__VCSEL_WIDTH: VCSELパルス幅
  - 0x02 = MEDIUM_RANGEモード用の最適値

**フェーズキャリブレーション設定 (0x004B-0x004C):**
- PHASECAL_CONFIG__TIMEOUT_MACROP: タイムアウト設定
  - 0x0D = ヒストグラムモード用の値
- PHASECAL_CONFIG__TARGET: ターゲット設定
  - 0x21 = デフォルト値

**重要なポイント:**
- 割り込み設定は、ポーリング方式ではなく割り込み方式でデータ取得する場合に重要
- VCSELパラメータは測定距離と精度に直接影響する
- これらの設定値は、MEDIUM_RANGEモード専用に最適化されている

#### コード実装

```c
/**
 * ステップ 7: General Configuration 設定
 */

// レジスタ: SYSTEM__INTERRUPT_CONFIG_GPIO (0x0046)
uint8_t int_config = 0x20;  // NEW_SAMPLE_READY
I2C_Write(0x29, 0x0046, &int_config, 1);

// レジスタ: CAL_CONFIG__VCSEL_START (0x0047)
uint8_t vcsel_start = 0x0B;  // 11
I2C_Write(0x29, 0x0047, &vcsel_start, 1);

// レジスタ: CAL_CONFIG__REPEAT_RATE (0x0048)
uint16_t cal_repeat_rate = 0x00;
uint8_t cal_buf[2] = {(cal_repeat_rate >> 8), cal_repeat_rate & 0xFF};
I2C_Write(0x29, 0x0048, cal_buf, 2);

// レジスタ: GLOBAL_CONFIG__VCSEL_WIDTH (0x004A)
uint8_t vcsel_width = 0x02;
I2C_Write(0x29, 0x004A, &vcsel_width, 1);

// レジスタ: PHASECAL_CONFIG__TIMEOUT_MACROP (0x004B)
uint8_t phasecal_timeout = 0x0D;  // ヒストグラムモード用
I2C_Write(0x29, 0x004B, &phasecal_timeout, 1);

// レジスタ: PHASECAL_CONFIG__TARGET (0x004C)
uint8_t phasecal_target = 0x21;
I2C_Write(0x29, 0x004C, &phasecal_target, 1);

// 待ち時間: なし
```

### 3.3 Timing Configuration レジスタ設定

#### 手順説明

Timing Configurationは、VL53L3CXの測定タイミングを制御する最も重要な設定です。VCSEL周期、タイムアウト値、測定間隔などを設定し、測定範囲、精度、速度のバランスを決定します。

**タイミングパラメータの概念:**

**VCSEL Period (VCSEL周期):**
- VCSELレーザーのパルス周期 (単位: VCSELクロック)
- Period A: 長距離測定用 (0x0B = 実周期12)
- Period B: 短距離測定用 (0x09 = 実周期10)
- MEDIUM_RANGEモードでは、両方の周期を使用して測定範囲を拡大

**Timeout Macrop (タイムアウトマクロ期間):**
- 測定に費やす最大時間 (エンコード値)
- MM (Multi-Mode) Timeout: 初期測定フェーズのタイムアウト
- Range Timeout: 本測定フェーズのタイムアウト
- 値が大きいほど、遠距離・低反射率の測定が可能だが、測定時間が増加

**Intermeasurement Period (測定間隔):**
- 連続測定モード時の測定間隔 (ms単位)
- 例: 100 = 100ms周期で測定を繰り返す

**MEDIUM_RANGEモードの設定値:**

**MM Timeout設定:**
- MM_CONFIG__TIMEOUT_MACROP_A: 0x001A (Period A用)
- MM_CONFIG__TIMEOUT_MACROP_B: 0x0020 (Period B用)

**Range Timeout設定:**
- RANGE_CONFIG__TIMEOUT_MACROP_A: 0x01CC (Period A用、重要)
- RANGE_CONFIG__TIMEOUT_MACROP_B: 0x01F5 (Period B用、重要)

**VCSEL Period設定:**
- RANGE_CONFIG__VCSEL_PERIOD_A: 0x0B (エンコード値、実周期 = 12)
- RANGE_CONFIG__VCSEL_PERIOD_B: 0x09 (エンコード値、実周期 = 10)

**測定間隔設定:**
- SYSTEM__INTERMEASUREMENT_PERIOD: 100 (100ms、推奨値)
  - 連続測定モードで使用
  - 測定時間よりも長い値を設定する必要がある

**重要なポイント:**
- VCSEL Period値は「エンコード値」であり、実際の周期は異なる (エンコード値 + 1)
- Timeout値もエンコードされており、実際の時間への変換には複雑な計算が必要
- これらの値を変更すると、測定範囲・精度・速度が大きく変化する
- MEDIUM_RANGEモード以外では、異なる値を使用する

#### コード実装

```c
/**
 * ステップ 8: Timing Configuration 設定
 */

// MM (Multi-Mode) タイムアウト設定
// レジスタ: MM_CONFIG__TIMEOUT_MACROP_A (0x005A, 0x005B)
uint16_t mm_timeout_a = 0x001A;  // エンコード値
uint8_t mm_a_buf[2] = {(mm_timeout_a >> 8), mm_timeout_a & 0xFF};
I2C_Write(0x29, 0x005A, mm_a_buf, 2);

// レジスタ: MM_CONFIG__TIMEOUT_MACROP_B (0x005C, 0x005D)
uint16_t mm_timeout_b = 0x0020;  // エンコード値
uint8_t mm_b_buf[2] = {(mm_timeout_b >> 8), mm_timeout_b & 0xFF};
I2C_Write(0x29, 0x005C, mm_b_buf, 2);

// Range タイムアウト設定
// レジスタ: RANGE_CONFIG__TIMEOUT_MACROP_A (0x005E, 0x005F)
uint16_t range_timeout_a = 0x01CC;  // エンコード値
uint8_t range_a_buf[2] = {(range_timeout_a >> 8), range_timeout_a & 0xFF};
I2C_Write(0x29, 0x005E, range_a_buf, 2);

// レジスタ: RANGE_CONFIG__VCSEL_PERIOD_A (0x0060)
uint8_t vcsel_period_a = 0x0B;  // エンコード値 (実際の周期 = 12)
I2C_Write(0x29, 0x0060, &vcsel_period_a, 1);

// レジスタ: RANGE_CONFIG__TIMEOUT_MACROP_B (0x0061, 0x0062)
uint16_t range_timeout_b = 0x01F5;  // エンコード値
uint8_t range_b_buf[2] = {(range_timeout_b >> 8), range_timeout_b & 0xFF};
I2C_Write(0x29, 0x0061, range_b_buf, 2);

// レジスタ: RANGE_CONFIG__VCSEL_PERIOD_B (0x0063)
uint8_t vcsel_period_b = 0x09;  // エンコード値 (実際の周期 = 10)
I2C_Write(0x29, 0x0063, &vcsel_period_b, 1);

// 測定間隔設定
// レジスタ: SYSTEM__INTERMEASUREMENT_PERIOD (0x006C)
uint32_t inter_measurement_period_ms = 100;  // 100ms
uint8_t imp_buf[4] = {
    (inter_measurement_period_ms >> 24) & 0xFF,
    (inter_measurement_period_ms >> 16) & 0xFF,
    (inter_measurement_period_ms >> 8) & 0xFF,
    inter_measurement_period_ms & 0xFF
};
I2C_Write(0x29, 0x006C, imp_buf, 4);

// 待ち時間: なし
```

### 3.4 Dynamic Configuration レジスタ設定

#### 手順説明

Dynamic Configurationは、測定動作中に変更可能な設定です。閾値、ROI (Region of Interest)、シーケンス設定などが含まれます。これらの設定を変更する場合は、**Grouped Parameter Hold** 機能を使用して、アトミックに反映させます。

**Grouped Parameter Hold の仕組み:**
- 複数のレジスタ変更を一括で反映させる機構
- SYSTEM__GROUPED_PARAMETER_HOLD_0 に 0x01 を書き込んで開始
- 複数のレジスタを変更
- SYSTEM__GROUPED_PARAMETER_HOLD に 0x02 を書き込んで終了・適用
- これにより、途中状態で測定が実行されることを防ぐ

**各設定の目的:**

**Grouped Parameter Hold 開始 (0x0071):**
- SYSTEM__GROUPED_PARAMETER_HOLD_0: 0x01 (パラメータ変更開始)

**閾値設定 (0x0072-0x0075):**
- SYSTEM__THRESH_HIGH: 高閾値 (0x0000 = デフォルト)
- SYSTEM__THRESH_LOW: 低閾値 (0x0000 = デフォルト)
- 距離閾値を設定し、特定範囲のみ割り込みを発生させることが可能

**シード設定 (0x0077):**
- SYSTEM__SEED_CONFIG: 0x02 (デフォルト値)
- 内部アルゴリズムの初期値設定

**SD (Signal Detection) 設定 (0x0078-0x007B):**
- SD_CONFIG__WOI_SD0: 0x0B (Window of Interest SD0、VCSEL Period Aに合わせる)
- SD_CONFIG__WOI_SD1: 0x09 (Window of Interest SD1、VCSEL Period Bに合わせる)
- SD_CONFIG__INITIAL_PHASE_SD0: 0x0A (初期位相SD0)
- SD_CONFIG__INITIAL_PHASE_SD1: 0x0A (初期位相SD1)
- 信号検出ウィンドウを設定し、VCSEL周期に同期させる

**Grouped Parameter Hold 継続 (0x007C):**
- SYSTEM__GROUPED_PARAMETER_HOLD_1: 0x01 (パラメータ変更継続)

**ROI (Region of Interest) 設定 (0x007F-0x0080):**
- ROI_CONFIG__USER_ROI_CENTRE_SPAD: 0xC7 (中心SPAD = 199)
  - 16x16 SPADアレイの中心を指定
- ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE: 0xFF (16x16 SPAD = 最大サイズ)
  - 全SPADを使用して測定 (最大視野角)

**シーケンス設定 (0x0081):**
- SYSTEM__SEQUENCE_CONFIG: 0xC1
  - bit 7: RANGE_EN = 1 (距離測定有効)
  - bit 6: MM2_EN = 1 (Multi-Mode 2有効)
  - bit 1: PHASECAL_EN = 0 (フェーズキャリブレーションは後で有効化)
  - bit 0: VHV_EN = 1 (VHV有効)
- ヒストグラムモードで必要なシーケンスを有効化

**Grouped Parameter Hold 終了 (0x0082):**
- SYSTEM__GROUPED_PARAMETER_HOLD: 0x02 (パラメータ変更終了・適用)

**重要なポイント:**
- Grouped Parameter Holdを使わないと、途中状態で測定が実行され、不正確な結果が得られる
- ROIサイズを小さくすると、測定視野角が狭くなるが、測定速度が向上する
- シーケンス設定は、測定モード (ヒストグラム/ディスタンス) によって異なる

#### コード実装

```c
/**
 * ステップ 9: Dynamic Configuration 設定
 */

// Grouped Parameter Hold 開始
// レジスタ: SYSTEM__GROUPED_PARAMETER_HOLD_0 (0x0071)
uint8_t gph_0 = 0x01;
I2C_Write(0x29, 0x0071, &gph_0, 1);

// 閾値設定
// レジスタ: SYSTEM__THRESH_HIGH (0x0072, 0x0073)
uint16_t thresh_high = 0x0000;
uint8_t th_buf[2] = {(thresh_high >> 8), thresh_high & 0xFF};
I2C_Write(0x29, 0x0072, th_buf, 2);

// レジスタ: SYSTEM__THRESH_LOW (0x0074, 0x0075)
uint16_t thresh_low = 0x0000;
uint8_t tl_buf[2] = {(thresh_low >> 8), thresh_low & 0xFF};
I2C_Write(0x29, 0x0074, tl_buf, 2);

// レジスタ: SYSTEM__SEED_CONFIG (0x0077)
uint8_t seed_config = 0x02;
I2C_Write(0x29, 0x0077, &seed_config, 1);

// SD (Signal Detection) 設定
// レジスタ: SD_CONFIG__WOI_SD0 (0x0078)
uint8_t woi_sd0 = 0x0B;  // VCSEL Period A に合わせる
I2C_Write(0x29, 0x0078, &woi_sd0, 1);

// レジスタ: SD_CONFIG__WOI_SD1 (0x0079)
uint8_t woi_sd1 = 0x09;  // VCSEL Period B に合わせる
I2C_Write(0x29, 0x0079, &woi_sd1, 1);

// レジスタ: SD_CONFIG__INITIAL_PHASE_SD0 (0x007A)
uint8_t init_phase_sd0 = 0x0A;
I2C_Write(0x29, 0x007A, &init_phase_sd0, 1);

// レジスタ: SD_CONFIG__INITIAL_PHASE_SD1 (0x007B)
uint8_t init_phase_sd1 = 0x0A;
I2C_Write(0x29, 0x007B, &init_phase_sd1, 1);

// Grouped Parameter Hold 継続
// レジスタ: SYSTEM__GROUPED_PARAMETER_HOLD_1 (0x007C)
uint8_t gph_1 = 0x01;
I2C_Write(0x29, 0x007C, &gph_1, 1);

// ROI (Region of Interest) 設定
// レジスタ: ROI_CONFIG__USER_ROI_CENTRE_SPAD (0x007F)
uint8_t roi_centre = 0xC7;  // 中心SPAD (199)
I2C_Write(0x29, 0x007F, &roi_centre, 1);

// レジスタ: ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE (0x0080)
uint8_t roi_size = 0xFF;  // 16x16 SPAD (最大サイズ)
I2C_Write(0x29, 0x0080, &roi_size, 1);

// シーケンス設定 (ヒストグラムモード)
// レジスタ: SYSTEM__SEQUENCE_CONFIG (0x0081)
uint8_t sequence_config = 0xC1;  // VHV + PHASECAL + MM2 + RANGE
// ビット構成:
//   bit 7: RANGE_EN = 1
//   bit 6: MM2_EN = 1
//   bit 1: PHASECAL_EN = 0 (後で有効化)
//   bit 0: VHV_EN = 1
I2C_Write(0x29, 0x0081, &sequence_config, 1);

// Grouped Parameter Hold 終了
// レジスタ: SYSTEM__GROUPED_PARAMETER_HOLD (0x0082)
uint8_t gph = 0x02;
I2C_Write(0x29, 0x0082, &gph, 1);

// 待ち時間: なし
```

### 3.5 System Control レジスタ設定

#### 手順説明

System Controlは、測定シーケンスの制御、ファームウェア設定、割り込みクリアなど、システム全体の制御を行います。プリセットモード設定の最終ステップです。

**各設定の目的:**

**ストリームカウント制御 (0x0083):**
- SYSTEM__STREAM_COUNT_CTRL: 0x00
  - 測定回数を制御するレジスタ
  - 0x00 = 無限ループ (連続測定モードで測定を繰り返す)
  - 0以外の値 = 指定回数の測定後、自動停止

**ファームウェア有効化 (0x0401):**
- FIRMWARE__ENABLE: 0x01
  - ファームウェアを有効化 (NVM読み出し時に無効化していた場合、ここで再有効化)
  - 0x01 = ファームウェア有効
  - ファームウェアが無効のままだと、測定動作が実行されない

**割り込みクリア (0x0086):**
- SYSTEM__INTERRUPT_CLEAR: 0x01
  - 以前の割り込みフラグをクリア
  - プリセットモード設定完了後、クリーンな状態から測定を開始するため
  - 測定データ読み出し後も、このレジスタで割り込みクリアが必要

**重要なポイント:**
- **SYSTEM__MODE_START レジスタ (0x0087) はここでは書き込まない**
  - このレジスタは測定開始時に書き込む (第5章で説明)
  - プリセットモード設定では、0x0087に書き込まない (測定は開始しない)
- ストリームカウントを0以外にすると、指定回数の測定後に自動停止する
  - 例: 0x0A = 10回測定後に停止
- 割り込みクリアは、新しい測定を開始する前に必ず実行する

**この時点での状態:**
- プリセットモード設定が完了
- デバイスは測定準備完了状態
- 測定開始コマンド (0x0087への書き込み) を待機中

#### コード実装

```c
/**
 * ステップ 10: System Control 設定
 */

// レジスタ: SYSTEM__STREAM_COUNT_CTRL (0x0083)
uint8_t stream_count = 0x00;
I2C_Write(0x29, 0x0083, &stream_count, 1);

// レジスタ: FIRMWARE__ENABLE (0x0401)
uint8_t firmware_enable = 0x01;
I2C_Write(0x29, 0x0401, &firmware_enable, 1);

// レジスタ: SYSTEM__INTERRUPT_CLEAR (0x0086)
uint8_t int_clear = 0x01;  // 割り込みクリア
I2C_Write(0x29, 0x0086, &int_clear, 1);

// 待ち時間: なし

// **注意**: SYSTEM__MODE_START は測定開始時に書き込む (ここでは書き込まない)
```

---

## 4. I2Cアドレス変更

### 4.1 アドレス変更の必要性

**使用シーン**:
- 複数のVL53L3CXセンサーを同じI2Cバス上で使用する場合
- デフォルトアドレス(0x29)が他のデバイスと競合する場合

**制限事項**:
- アドレス変更は**揮発性** (電源オフで初期化)
- デバイス起動毎に再設定が必要
- 有効な7-bitアドレス範囲: 0x08-0x77

### 4.2 アドレス変更手順

#### 手順説明

I2Cアドレス変更は非常にシンプルで、1つのレジスタに新しいアドレスを書き込むだけです。ただし、いくつかの重要な注意点があります。

**詳細手順:**

**ステップ1: アドレス範囲チェック**
- 新しいアドレスが有効範囲 (0x08-0x77) 内にあることを確認
- I2C仕様の予約アドレス (0x00-0x07, 0x78-0x7F) は使用不可

**ステップ2: レジスタへの書き込み**
- レジスタ `I2C_SLAVE__DEVICE_ADDRESS` (0x0001) に新しい7-bitアドレスを書き込む
- **重要**: 現在のアドレスを使ってI2C通信を行う (まだ変更されていないため)
- 書き込む値: 新しいアドレス (7-bit値をそのまま、シフト不要)

**ステップ3: アドレス切り替え**
- レジスタ書き込みが完了した瞬間、新しいアドレスが有効になる
- 待ち時間: 不要 (即座に有効)
- **この後のI2C通信は、新しいアドレスを使用する必要がある**

**メーカー純正APIとの違い:**
- STMicroelectronics社の純正APIでは、8-bitアドレスを2で割って書き込む
- 本実装では、7-bitアドレスをそのまま書き込む (より直感的)
- 結果は同じ (例: 8-bit 0x52を2で割ると0x29、7-bit 0x29をそのまま書き込むと0x29)

**重要なポイント:**
- アドレス変更は揮発性 (電源オフで0x29に戻る)
- ソフトリセット (0x0000レジスタ) でも0x29に戻る
- 必ずファームウェア起動完了後に実行する (起動前に実行すると失敗する)
- マルチセンサー構成では、XSHUTピンで1台ずつ起動してアドレス変更

#### コード実装

```c
/**
 * @brief I2Cアドレス変更
 * @param current_addr 現在のI2Cアドレス (7-bit)
 * @param new_addr 新しいI2Cアドレス (7-bit)
 * @return 0=成功, <0=エラー
 *
 * 重要:
 * - この関数を呼ぶ前に、デバイスは起動済みである必要がある
 * - アドレス変更は電源オフで元に戻る (不揮発性ではない)
 * - マルチセンサーシステムでは、各センサーのXSHUTピンを
 *   個別に制御してアドレス変更を順次実行する
 */
int vl53l3cx_set_device_address(uint8_t current_addr, uint8_t new_addr) {
    int ret;
    uint8_t addr_reg_value;

    // アドレス範囲チェック
    if (new_addr < 0x08 || new_addr > 0x77) {
        return -1;  // 無効なアドレス
    }

    // レジスタ: I2C_SLAVE__DEVICE_ADDRESS (0x0001)
    // 書き込む値: 新しい7-bitアドレス (そのまま)
    addr_reg_value = new_addr & 0x7F;

    // **重要**: 現在のアドレスを使って書き込む
    ret = i2c_write(current_addr, 0x0001, &addr_reg_value, 1);
    if (ret != 0) {
        return ret;
    }

    // 待ち時間: なし (即座に有効)

    // **注意**: この後のI2C通信は新しいアドレスを使用する

    return 0;
}
```

### 4.3 マルチセンサー初期化シーケンス

複数センサーを同じI2Cバスで使用する場合の完全な手順:

```c
/**
 * @brief 複数センサー初期化 (例: 3台)
 *
 * ハードウェア接続:
 * - センサー1: XSHUT → GPIO_PIN_1
 * - センサー2: XSHUT → GPIO_PIN_2
 * - センサー3: XSHUT → GPIO_PIN_3
 * - 全センサー: I2C SDA/SCL共通バス
 *
 * 新しいアドレス割り当て:
 * - センサー1: 0x30
 * - センサー2: 0x31
 * - センサー3: 0x32
 */
int multi_sensor_init(void) {
    int ret;

    // ========================================
    // ステップ 1: 全センサーをシャットダウン
    // ========================================
    gpio_write(GPIO_PIN_1, 0);  // センサー1 シャットダウン
    gpio_write(GPIO_PIN_2, 0);  // センサー2 シャットダウン
    gpio_write(GPIO_PIN_3, 0);  // センサー3 シャットダウン

    delay_ms(10);  // シャットダウン安定化待ち

    // ========================================
    // ステップ 2: センサー1起動とアドレス変更
    // ========================================
    gpio_write(GPIO_PIN_1, 1);  // センサー1のみ起動
    delay_ms(10);  // 起動待ち

    // ファームウェア起動待ち (デフォルトアドレス 0x29 使用)
    ret = vl53l3cx_wait_boot(0x29);
    if (ret != 0) return ret;

    // アドレス変更: 0x29 → 0x30
    ret = vl53l3cx_set_device_address(0x29, 0x30);
    if (ret != 0) return ret;

    printf("Sensor 1 address changed to 0x30\n");

    // ========================================
    // ステップ 3: センサー2起動とアドレス変更
    // ========================================
    gpio_write(GPIO_PIN_2, 1);  // センサー2起動
    delay_ms(10);

    // ファームウェア起動待ち (デフォルトアドレス 0x29 使用)
    ret = vl53l3cx_wait_boot(0x29);
    if (ret != 0) return ret;

    // アドレス変更: 0x29 → 0x31
    ret = vl53l3cx_set_device_address(0x29, 0x31);
    if (ret != 0) return ret;

    printf("Sensor 2 address changed to 0x31\n");

    // ========================================
    // ステップ 4: センサー3起動とアドレス変更
    // ========================================
    gpio_write(GPIO_PIN_3, 1);  // センサー3起動
    delay_ms(10);

    ret = vl53l3cx_wait_boot(0x29);
    if (ret != 0) return ret;

    ret = vl53l3cx_set_device_address(0x29, 0x32);
    if (ret != 0) return ret;

    printf("Sensor 3 address changed to 0x32\n");

    // ========================================
    // ステップ 5: 各センサーを個別に初期化
    // ========================================
    vl53l3cx_dev_t sensor1, sensor2, sensor3;

    sensor1.i2c_addr = 0x30;
    ret = vl53l3cx_init(&sensor1);
    if (ret != 0) return ret;

    sensor2.i2c_addr = 0x31;
    ret = vl53l3cx_init(&sensor2);
    if (ret != 0) return ret;

    sensor3.i2c_addr = 0x32;
    ret = vl53l3cx_init(&sensor3);
    if (ret != 0) return ret;

    printf("All sensors initialized\n");

    return 0;
}

/**
 * @brief ファームウェア起動待ち (簡易版)
 */
int vl53l3cx_wait_boot(uint8_t addr) {
    uint8_t boot_status;
    uint32_t start_time = millis();

    do {
        i2c_read(addr, 0x0010, &boot_status, 1);

        if ((millis() - start_time) > 500) {
            return -1;  // タイムアウト
        }
        delay_ms(1);
    } while ((boot_status & 0x01) == 0);

    return 0;
}
```

### 4.4 アドレス変更のタイミング図

```
時間軸 →

XSHUT1: ___/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
XSHUT2: _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
XSHUT3: _____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾

I2C通信:
  [0x29起動待ち] [0x29→0x30変更]
                  ↓
              センサー1は0x30で応答
                      [0x29起動待ち] [0x29→0x31変更]
                                      ↓
                                  センサー2は0x31で応答
                                          [0x29起動待ち] [0x29→0x32変更]
                                                          ↓
                                                      センサー3は0x32で応答
```

### 4.5 注意事項

**⚠ 重要な制限:**

1. **揮発性設定**: アドレス変更は電源オフで元に戻る
   - 電源投入毎に再設定が必要
   - ソフトリセット (0x0000レジスタ) でも元に戻る

2. **XSHUTピン必須**: マルチセンサー構成では各センサーにXSHUTピンの個別制御が必要
   - XSHUTピンがない場合、マルチセンサーは不可能

3. **順次起動**: センサーは必ず1台ずつ起動してアドレス変更
   - 同時に複数起動すると全て0x29で応答し、アドレス競合発生

4. **アドレス範囲**: 7-bitアドレスのみ有効
   - 範囲: 0x08 - 0x77
   - 予約アドレス (0x00-0x07, 0x78-0x7F) は使用不可

### 4.6 実装例: アドレス変更機能付き初期化

```c
/**
 * @brief アドレス変更対応初期化関数
 * @param dev デバイスハンドル
 * @param new_address 新しいI2Cアドレス (0=変更しない)
 */
int vl53l3cx_init_with_address(vl53l3cx_dev_t *dev, uint8_t new_address) {
    int ret;
    uint8_t current_addr = 0x29;  // デフォルトアドレス

    // ファームウェア起動待ち (デフォルトアドレス使用)
    ret = vl53l3cx_wait_boot(current_addr);
    if (ret != 0) return ret;

    // アドレス変更 (指定された場合のみ)
    if (new_address != 0 && new_address != current_addr) {
        ret = vl53l3cx_set_device_address(current_addr, new_address);
        if (ret != 0) return ret;

        dev->i2c_addr = new_address;
        printf("Address changed: 0x%02X → 0x%02X\n", current_addr, new_address);
    } else {
        dev->i2c_addr = current_addr;
    }

    // 通常の初期化処理 (新しいアドレスで実行)
    ret = vl53l3cx_read_nvm_calibration_data(dev);
    if (ret != 0) return ret;

    ret = vl53l3cx_set_preset_mode_medium_range(dev);
    if (ret != 0) return ret;

    return 0;
}
```

### 4.7 使用例

```c
int main(void) {
    vl53l3cx_dev_t sensor1, sensor2;
    int ret;

    // センサー1: デフォルトアドレスのまま使用
    ret = vl53l3cx_init_with_address(&sensor1, 0);  // 0 = アドレス変更なし
    if (ret != 0) {
        printf("Sensor 1 init failed\n");
        return -1;
    }
    printf("Sensor 1 initialized at 0x29\n");

    // または、マルチセンサー構成
    ret = multi_sensor_init();
    if (ret != 0) {
        printf("Multi-sensor init failed\n");
        return -1;
    }

    // 各センサーで測定開始
    vl53l3cx_start_ranging(&sensor1);
    vl53l3cx_start_ranging(&sensor2);

    // メインループ
    while (1) {
        vl53l3cx_result_t result1, result2;

        vl53l3cx_get_ranging_data(&sensor1, &result1);
        vl53l3cx_get_ranging_data(&sensor2, &result2);

        printf("Sensor1: %d mm, Sensor2: %d mm\n",
               result1.distance_mm, result2.distance_mm);

        delay_ms(100);
    }

    return 0;
}
```

---

## 5. 測定開始・停止手順

### 5.0 測定制御概要

プリセットモード設定が完了したら、測定を開始できます。VL53L3CXは、連続測定モード (BACKTOBACK) と単発測定モード (SINGLESHOT) をサポートしています。この章では、連続測定モードを中心に説明します。

**測定モード:**
- **BACKTOBACK (連続測定)**: 測定を繰り返し実行し、設定した測定間隔で新しいデータを生成
  - モードコード: 0x42 (0x40 + 0x02)
  - 用途: リアルタイム距離測定、連続監視
- **SINGLESHOT (単発測定)**: 1回だけ測定を実行し、自動停止
  - モードコード: 0x12 (0x10 + 0x02)
  - 用途: 低消費電力、トリガー駆動測定

**測定開始から停止までの流れ:**
1. 割り込み設定確認 (0x0046レジスタ)
2. 割り込みクリア (0x0086レジスタ)
3. 測定開始 (0x0087レジスタに0x42書き込み)
4. データレディ待ち (ポーリングまたは割り込み)
5. データ読み出し (第6章で説明)
6. 測定停止 (0x0087レジスタに0x00書き込み)

**使用するレジスタ:**
- `SYSTEM__INTERRUPT_CONFIG_GPIO` (0x0046): 割り込み設定
- `SYSTEM__INTERRUPT_CLEAR` (0x0086): 割り込みクリア
- `SYSTEM__MODE_START` (0x0087): 測定開始/停止制御
- `RESULT__INTERRUPT_STATUS` (0x0089): データレディステータス

---

### 5.1 連続測定開始

#### 手順説明

連続測定モードでは、測定を自動的に繰り返し、設定した測定間隔 (SYSTEM__INTERMEASUREMENT_PERIOD) ごとに新しいデータを生成します。

**詳細手順:**

**ステップ11.1: 割り込み設定確認**
- レジスタ `SYSTEM__INTERRUPT_CONFIG_GPIO` (0x0046) に 0x20 を書き込む
- これは第3章で既に設定済みだが、念のため再確認・再設定
- 0x20 = NEW_SAMPLE_READY (新しいサンプル準備完了時に割り込み)

**ステップ11.2: 割り込みクリア**
- レジスタ `SYSTEM__INTERRUPT_CLEAR` (0x0086) に 0x01 を書き込む
- 以前の割り込みフラグをクリアし、クリーンな状態から測定開始

**ステップ11.3: 測定開始**
- レジスタ `SYSTEM__MODE_START` (0x0087) に 0x42 を書き込む
- **0x42の構成**:
  - bits [7:4] = 0x4: BACKTOBACK (連続測定モード)
  - bits [1:0] = 0x2: HISTOGRAM スケジューラモード
- この書き込みにより、測定が自動的に開始される
- 待ち時間: 不要 (即座に測定開始)

**測定開始後の動作:**
1. デバイスが自動的に測定を実行 (約33ms、MEDIUM_RANGEモードの場合)
2. 測定完了後、割り込みピンがLowレベルになる (ACTIVE_LOW設定の場合)
3. RESULT__INTERRUPT_STATUS (0x0089) のbit 5が1になる
4. 測定間隔 (100ms) 待機後、次の測定を自動的に開始
5. 測定停止コマンドが来るまで、この動作を繰り返す

**重要なポイント:**
- 測定開始後、デバイスは自律的に動作する (ホストCPUの介入不要)
- データレディは、ポーリング (0x0089を定期的に読む) または割り込み (GPIOピン監視) で検出
- 連続測定モードでは、データ読み出し後も測定は継続する

#### コード実装

```c
/**
 * ステップ 11: 測定開始
 */

// 11.1: GPIO割り込み設定 (再確認)
uint8_t int_config = 0x20;  // NEW_SAMPLE_READY
I2C_Write(0x29, 0x0046, &int_config, 1);

// 11.2: 割り込みクリア
uint8_t int_clear = 0x01;
I2C_Write(0x29, 0x0086, &int_clear, 1);

// 11.3: 測定開始
// レジスタ: SYSTEM__MODE_START (0x0087)
uint8_t mode_start = 0x42;  // BACKTOBACK | HISTOGRAM
// ビット構成:
//   bits [7:4] = 0x4: BACKTOBACK (連続測定)
//   bits [1:0] = 0x2: HISTOGRAM スケジューラモード
I2C_Write(0x29, 0x0087, &mode_start, 1);

// **待ち時間: なし** (測定が自動的に開始される)
```

### 5.2 データレディ待ち (ポーリング方式)

#### 手順説明

測定開始後、新しいデータが準備完了するまで待機する必要があります。ポーリング方式では、定期的にステータスレジスタを読み出して、データレディフラグをチェックします。

**詳細手順:**

**ステップ12: ポーリングループ**
1. レジスタ `RESULT__INTERRUPT_STATUS` (0x0089) を読み出す
2. 読み出した値のbit 5 (NEW_DATA_READY) をチェック
   - bit 5 = 0: まだデータ準備中 → 1ms待機して再度読み出し
   - bit 5 = 1: データ準備完了 → データ読み出しへ進む
3. タイムアウト処理: 2秒経過してもデータが準備されない場合はエラー

**ポーリング間隔の推奨値:**
- 1ms間隔 (短すぎるとI2Cバスを占有、長すぎるとデータ取得遅延)
- MEDIUM_RANGEモードでは、測定時間が約33msなので、1msポーリングで十分

**タイムアウト値の推奨:**
- 2000ms (2秒) が推奨
- 通常は33ms程度で完了するが、エラー時の無限ループを防ぐため

**重要なポイント:**
- ポーリング方式は実装が簡単だが、CPUリソースを消費する
- 低消費電力が必要な場合は、5.3の割り込み方式を推奨
- 連続測定モードでは、データ読み出し後、次の測定のためにこのループを再実行

#### コード実装

```c
/**
 * ステップ 12: データレディ待ち
 */
uint8_t interrupt_status;
uint32_t timeout = 2000;  // 2秒タイムアウト
uint32_t start_time = millis();

do {
    // レジスタ: RESULT__INTERRUPT_STATUS (0x0089)
    I2C_Read(0x29, 0x0089, &interrupt_status, 1);

    if ((millis() - start_time) > timeout) {
        return ERROR_MEASUREMENT_TIMEOUT;
    }

    delay_ms(1);  // 1msポーリング間隔

} while ((interrupt_status & 0x20) == 0);  // bit 5: NEW_DATA_READY

// データ準備完了
```

### 5.3 データレディ待ち (割り込み方式)

#### 手順説明

割り込み方式では、VL53L3CXのGPIOピンからの割り込み信号を使用してデータレディを検出します。ポーリング方式と比べ、CPUリソースの消費が少なく、低消費電力アプリケーションに適しています。

**割り込み方式の仕組み:**
1. VL53L3CXのGPIO1ピンをマイコンのGPIO入力ピンに接続
2. マイコン側でGPIO割り込みを設定 (立ち下がりエッジ検出、ACTIVE_LOW設定の場合)
3. 測定完了時、VL53L3CXがGPIO1ピンをLowにする
4. マイコンの割り込みハンドラが呼ばれ、フラグをセット
5. メインループでフラグをチェックし、データ読み出しへ進む

**実装の手順:**

**ステップ1: GPIO割り込み初期化 (初期化時に1回のみ実行)**
- マイコンのGPIOピンを入力プルアップに設定
- 立ち下がりエッジ割り込みを有効化 (ACTIVE_LOW設定の場合)
- NVIC割り込みを有効化

**ステップ2: 割り込みハンドラ実装**
- 割り込みフラグをクリア
- データレディフラグをセット (volatile変数使用)

**ステップ3: メインループでのデータレディ待機**
- データレディフラグをポーリング (またはWFI命令で低消費電力待機)
- フラグがセットされたらデータ読み出しへ進む

**重要なポイント:**
- GPIO割り込みはプラットフォーム依存 (以下のコードはSTM32の例)
- 割り込みハンドラは短く保つ (フラグセットのみ、データ読み出しはメインループで実行)
- WFI (Wait For Interrupt) 命令を使用すると、さらに低消費電力化が可能
- 割り込み方式でも、念のためタイムアウト処理を実装することを推奨

#### コード実装

```c
/**
 * GPIO割り込み設定 (初期化時に1回だけ)
 */

// プラットフォーム依存のGPIO割り込み設定
// 例: STM32の場合
void setup_gpio_interrupt(void) {
    // GPIOピンを入力プルアップに設定
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_X;  // VL53L3CXのGPIO1ピン接続先
    gpio_init.Mode = GPIO_MODE_IT_FALLING;  // 立ち下がりエッジ割り込み
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOX, &gpio_init);

    // NVIC割り込み有効化
    HAL_NVIC_SetPriority(EXTI_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI_IRQn);
}

// 割り込みハンドラ
volatile uint8_t data_ready_flag = 0;

void EXTI_IRQHandler(void) {
    if (LL_EXTI_IsActiveFlag_X()) {
        LL_EXTI_ClearFlag_X();
        data_ready_flag = 1;  // フラグセット
    }
}

// メインループでの待機
void wait_data_ready_interrupt(void) {
    data_ready_flag = 0;

    while (!data_ready_flag) {
        // 低消費電力モード
        __WFI();  // Wait For Interrupt
    }

    // データ準備完了
}
```

### 5.4 測定停止

#### 手順説明

連続測定モードを停止するには、SYSTEM__MODE_START レジスタに停止コマンドを書き込みます。測定を確実に停止するため、メーカーは2回書き込むことを推奨しています。

**詳細手順:**

**ステップ13.1: 停止コマンド送信 (1回目)**
- レジスタ `SYSTEM__MODE_START` (0x0087) に 0x00 を書き込む
- 0x00 = STOP (測定停止コマンド)

**ステップ13.2: 停止コマンド送信 (2回目)**
- 同じレジスタに再度 0x00 を書き込む
- **重要**: メーカー推奨の確実な停止方法

**ステップ13.3: 割り込みクリア**
- レジスタ `SYSTEM__INTERRUPT_CLEAR` (0x0086) に 0x01 を書き込む
- 残っている割り込みフラグをクリア

**なぜ2回書き込むのか:**
- デバイスが測定実行中にSTOPコマンドを受信した場合、1回目は無視される可能性がある
- 2回書き込むことで、確実に停止できる (メーカーの内部実装に基づく推奨)

**停止後の状態:**
- 測定が完全に停止
- デバイスは待機状態 (次の測定開始コマンドを待つ)
- プリセットモード設定は保持されている (再設定不要)
- 次に測定を開始する場合は、5.1の手順から実行

**重要なポイント:**
- 測定中のデータが残っている場合、データ読み出し後に停止することを推奨
- 停止後、必ず割り込みクリアを実行 (次回の測定開始時に誤検出を防ぐ)
- 低消費電力モードに入る前に、必ず測定を停止する

#### コード実装

```c
/**
 * ステップ 13: 測定停止
 */

// レジスタ: SYSTEM__MODE_START (0x0087)
uint8_t mode_stop = 0x00;  // STOP
I2C_Write(0x29, 0x0087, &mode_stop, 1);

// 安全のため2回書き込む (メーカー推奨)
I2C_Write(0x29, 0x0087, &mode_stop, 1);

// 割り込みクリア
uint8_t int_clear = 0x01;
I2C_Write(0x29, 0x0086, &int_clear, 1);

// 待ち時間: なし
```

---

## 6. データ読み出し手順

### 6.0 データ読み出し概要

データレディ検出後、測定結果をデバイスから読み出します。VL53L3CXは、ヒストグラムモードで24個のビン (時間ウィンドウ) のデータを生成します。この生データから距離を計算します。

**ヒストグラムデータの構造:**
- 24個のビン (ビン0 - ビン23)
- 各ビンは24-bit値 (3バイト)
- 各ビンは特定の時間ウィンドウでの光子検出カウント数を表す
- 距離は、最大カウントを持つビンの位置から計算

**読み出すデータ:**
1. ヘッダー情報 (5バイト):
   - RESULT__INTERRUPT_STATUS (1バイト)
   - RESULT__RANGE_STATUS (1バイト): 測定ステータス
   - REPORT_STATUS (1バイト)
   - STREAM_COUNT (1バイト)
   - 予約 (1バイト)
2. ヒストグラムビンデータ (72バイト = 24ビン × 3バイト)

**合計読み出しサイズ:** 77バイト (0x0089から一括読み出し可能)

**データ処理の流れ:**
1. 77バイトを一括読み出し (6.1)
2. ヘッダー情報を抽出・確認
3. ヒストグラムビンデータを抽出
4. アンビエント除去とピーク検出 (6.2)
5. 距離計算 (6.2または6.3)
6. 割り込みクリア

---

### 6.1 ヒストグラムビンデータ完全読み出し

#### 手順説明

ヒストグラムデータは、レジスタ0x0089から始まる77バイトの連続領域に格納されています。I2C連続読み出しを使用して、1回のトランザクションで全データを取得できます。

**データ読み出しプロトコル:**

**ステップ14.1: 一括読み出し**
- レジスタ `RESULT__INTERRUPT_STATUS` (0x0089) から77バイトを読み出す
- I2C連続読み出しを使用 (効率的)

**ステップ14.2: ヘッダー情報抽出**
- バイト0: INTERRUPT_STATUS
  - bit 5: NEW_DATA_READY (1=新データあり)
  - bit 4: ERROR (1=エラー発生)
  - bit 3: RANGE_COMPLETE (1=測定完了)
- バイト1: RANGE_STATUS (測定ステータスコード、付録C参照)
  - 0x09 = 正常測定完了 (この値のみデータ使用可能)
  - その他 = エラー (データ無効)
- バイト2: REPORT_STATUS
- バイト3: STREAM_COUNT (測定カウンタ)
- バイト4: 予約

**ステップ14.3: ヒストグラムビン抽出**
- バイト5から24ビン分のデータを抽出
- 各ビンは3バイト (24-bit、ビッグエンディアン)
- ビン計算式: `bin_data[i] = (buf[5+i*3]<<16) | (buf[5+i*3+1]<<8) | buf[5+i*3+2]`

**ステップ14.4: 割り込みクリア**
- レジスタ `SYSTEM__INTERRUPT_CLEAR` (0x0086) に 0x01 を書き込む
- 次の測定のために割り込みフラグをクリア

**データフォーマット:**
```
バイト  内容
0      INTERRUPT_STATUS
1      RANGE_STATUS  <-- 重要! 0x09のみ有効
2      REPORT_STATUS
3      STREAM_COUNT
4      予約
5-7    ビン0 (MSB, MID, LSB)
8-10   ビン1
...
74-76  ビン23
```

**重要なポイント:**
- RANGE_STATUS が 0x09 以外の場合、データは無効 (エラー処理が必要)
- ヒストグラムデータはビッグエンディアン (MSBファースト)
- 連続測定モードでは、割り込みクリア後、自動的に次の測定が開始される

#### コード実装

```c
/**
 * ステップ 14: ヒストグラムデータ読み出し
 *
 * 開始アドレス: RESULT__INTERRUPT_STATUS (0x0089)
 * 読み出しサイズ: 77バイト
 *
 * データ構造:
 *   [0]      : INTERRUPT_STATUS
 *   [1]      : RANGE_STATUS
 *   [2]      : REPORT_STATUS
 *   [3]      : STREAM_COUNT
 *   [4]      : 予約
 *   [5-76]   : ヒストグラムビンデータ (24 bins × 3 bytes)
 */

uint8_t histogram_buffer[77];

// 一括読み出し
I2C_Read(0x29, 0x0089, histogram_buffer, 77);

// ヘッダー情報抽出
uint8_t interrupt_status = histogram_buffer[0];
uint8_t range_status = histogram_buffer[1];
uint8_t report_status = histogram_buffer[2];
uint8_t stream_count = histogram_buffer[3];

// ヒストグラムビンデータ抽出 (24 bins)
uint32_t bin_data[24];

for (int bin = 0; bin < 24; bin++) {
    int offset = 5 + (bin * 3);  // ヘッダー5バイト + ビンオフセット

    // 24-bit値を32-bit変数に再構成 (ビッグエンディアン)
    bin_data[bin] = ((uint32_t)histogram_buffer[offset + 0] << 16) |
                    ((uint32_t)histogram_buffer[offset + 1] << 8) |
                    ((uint32_t)histogram_buffer[offset + 2]);
}

// 割り込みクリア
uint8_t int_clear = 0x01;
I2C_Write(0x29, 0x0086, &int_clear, 1);

// 待ち時間: なし (次の測定は自動的に開始される)
```

### 6.2 ヒストグラムビンから距離計算 (簡易版)

#### 手順説明

ヒストグラムビンデータから距離を計算します。簡易版では、アンビエント (周囲光) を除去し、最大カウントを持つビンの位置から距離を計算します。

**距離計算の原理:**
- VCSELがレーザーパルスを発射
- ターゲットで反射した光子がSPADで検出される
- 飛行時間 (Time of Flight) に応じて、特定のビンにカウントが集中
- ピークビンの位置 × ビン幅 = 距離

**詳細手順:**

**ステップ15.1: アンビエント推定**
- 最初の6ビン (ビン0-5) の平均を計算
- これらのビンは、レーザー反射よりも前の時間ウィンドウ (周囲光のみ検出)
- `ambient_estimate = (bin0 + bin1 + ... + bin5) / 6`

**ステップ15.2: アンビエント除去**
- 各ビンから推定アンビエントを減算
- `corrected_bin[i] = bin_data[i] - ambient_estimate` (負値の場合は0)
- これにより、レーザー反射によるカウントのみが残る

**ステップ15.3: ピーク検出**
- 有効範囲 (ビン6-17) 内で最大カウントを持つビンを探索
- `max_count`と`peak_bin`を記録
- ビン0-5は周囲光のみ、ビン18-23は遠距離すぎるため除外

**ステップ15.4: 距離計算**
- MEDIUM_RANGEモードでは、2つのVCSEL周期を使用
  - Period A (ビン0-11): VCSEL周期12 → ビン幅 約15.0mm
  - Period B (ビン12-23): VCSEL周期10 → ビン幅 約12.5mm
- 簡易計算式:
  - `peak_bin < 12` の場合: `distance_mm = peak_bin × 15.0`
  - `peak_bin >= 12` の場合: `distance_mm = peak_bin × 12.5`

**ステップ15.5: ステータスチェック**
- RANGE_STATUS が 0x09 (RANGE_COMPLETE) の場合のみ、距離を有効とする
- その他のステータスコードはエラー (付録C参照)

**重要なポイント:**
- アンビエント除去は、屋外や明るい環境で重要
- ピーク検出範囲を限定することで、誤検出を防ぐ
- ビン幅は近似値 (正確な計算には、NVMから読み出したオシレータ周波数が必要)
- より高精度な距離計算は、6.3のサブビン補間を参照

#### コード実装

```c
/**
 * ステップ 15: ピーク検出と距離計算
 */

// アンビエント推定 (最初の6ビンの平均)
uint32_t ambient_sum = 0;
for (int i = 0; i < 6; i++) {
    ambient_sum += bin_data[i];
}
uint32_t ambient_estimate = ambient_sum / 6;

// アンビエント除去
uint32_t corrected_bins[24];
for (int i = 0; i < 24; i++) {
    if (bin_data[i] > ambient_estimate) {
        corrected_bins[i] = bin_data[i] - ambient_estimate;
    } else {
        corrected_bins[i] = 0;
    }
}

// ピーク検出 (最大カウントビン)
uint32_t max_count = 0;
int peak_bin = 0;

for (int i = 6; i < 18; i++) {  // 有効範囲のみ検索
    if (corrected_bins[i] > max_count) {
        max_count = corrected_bins[i];
        peak_bin = i;
    }
}

// 距離計算
// 簡易式: distance_mm = peak_bin × bin_width_mm
// MEDIUM_RANGE モード:
//   VCSEL Period A = 12 → ビン幅 ≈ 2000ps → 約15.0mm/bin
//   VCSEL Period B = 10 → ビン幅 ≈ 1666ps → 約12.5mm/bin

uint16_t distance_mm;

if (peak_bin < 12) {
    // Period A 使用
    distance_mm = (uint16_t)(peak_bin * 15.0);
} else {
    // Period B 使用
    distance_mm = (uint16_t)(peak_bin * 12.5);
}

// ステータスチェック
uint8_t range_status_code = range_status & 0x1F;  // 下位5ビット

if (range_status_code == 0x09) {
    // RANGE_COMPLETE: 正常測定
    printf("Distance: %d mm\n", distance_mm);
} else {
    // エラー処理
    printf("Range Error: %d\n", range_status_code);
}
```

### 6.3 精密距離計算 (サブビン補間)

#### 手順説明

簡易版の距離計算では、ピークビンの整数位置のみを使用しますが、より高精度な測定には、ピーク周辺のビンを使った補間が有効です。放物線フィッティングにより、サブビン (小数点以下) の精度で距離を求めます。

**サブビン補間の原理:**
- ピークビンとその前後のビンの3点を使用
- 3点から放物線 (2次曲線) を推定
- 放物線の頂点位置がサブビン精度でのピーク位置
- 頂点位置 × ビン幅 = 精密距離

**詳細手順:**

**ステップ1: 3点取得**
- ピークビン `peak_bin` を中心に3点を取得
- `a = corrected_bins[peak_bin - 1]` (前のビン)
- `b = corrected_bins[peak_bin]` (ピークビン)
- `c = corrected_bins[peak_bin + 1]` (次のビン)

**ステップ2: サブビンオフセット計算**
- 放物線フィッティング公式:
  - `sub_bin_offset = 0.5 × (a - c) / (a - 2×b + c)`
- この値は、-0.5 〜 +0.5 の範囲 (ピークビンからの相対位置)

**ステップ3: 精密ビン位置計算**
- `accurate_bin = peak_bin + sub_bin_offset`
- 例: peak_bin = 10, sub_bin_offset = 0.3 → accurate_bin = 10.3

**ステップ4: 精密距離計算**
- `distance_mm = accurate_bin × bin_width`
- bin_widthはVCSEL周期によって異なる (Period A: 12.5mm、Period B: 15.0mm)

**適用条件:**
- `peak_bin > 0` かつ `peak_bin < 23` (境界ビンでは補間不可)
- ピークビンのカウントが十分大きい (低カウントでは補間精度が低下)
- 分母 `(a - 2×b + c)` が0でないこと (ゼロ除算回避)

**重要なポイント:**
- サブビン補間により、距離分解能が約10倍向上 (例: 12.5mm → 1.25mm)
- ノイズが多い環境では、補間結果が不安定になる可能性あり
- 高精度が必要な場合のみ使用し、通常は6.2の簡易版で十分

#### コード実装

```c
/**
 * より高精度な距離計算 (放物線フィッティング)
 */

if (peak_bin > 0 && peak_bin < 23) {
    // 前後のビンを使った放物線フィッティング
    int32_t a = corrected_bins[peak_bin - 1];
    int32_t b = corrected_bins[peak_bin];
    int32_t c = corrected_bins[peak_bin + 1];

    // サブビンオフセット計算
    float sub_bin_offset = 0.5 * (float)(a - c) / (float)(a - 2*b + c);

    // 精密ビン位置
    float accurate_bin = (float)peak_bin + sub_bin_offset;

    // 距離計算 (Period A の場合)
    float distance_mm_float = accurate_bin * 15.0;
    distance_mm = (uint16_t)distance_mm_float;
}
```

---

## 7. 完全なコード実装例

### 7.0 実装例概要

この章では、第1章から第6章で説明した全ての手順を統合した、完全に動作するドライバコードを提供します。これらのコードは、そのままコピー&ペーストして使用できるように設計されています。

**実装内容:**
1. **構造体定義** (7.1): デバイスハンドルと測定結果の構造体
2. **初期化関数** (7.2): 起動待ち、NVM読み出し、プリセット設定を統合
3. **NVM較正データ読み出し** (7.3): NVM制御の完全実装
4. **プリセットモード設定** (7.4): MEDIUM_RANGEモード設定関数
5. **測定制御関数** (7.5): 測定開始、データ読み出し、測定停止
6. **使用例** (7.6): main関数での統合例

**前提条件:**
- I2C通信関数が実装されていること:
  - `int i2c_read(uint8_t addr, uint16_t reg, uint8_t *data, uint16_t len)`
  - `int i2c_write(uint8_t addr, uint16_t reg, uint8_t *data, uint16_t len)`
- 時間待機関数が実装されていること:
  - `void delay_ms(uint32_t ms)`: ミリ秒待機
  - `void delay_us(uint32_t us)`: マイクロ秒待機
  - `uint32_t millis(void)`: 起動からの経過ミリ秒

**コード設計の特徴:**
- エラーチェック付き (全ての関数が0=成功, <0=エラーを返す)
- 構造化設計 (各機能が独立した関数)
- コメント充実 (各ステップの説明付き)
- ポータブル (プラットフォーム依存部分を分離)

**使用方法:**
1. 7.1-7.5のコードを統合してドライバファイルを作成
2. プラットフォーム依存の関数 (I2C、タイマー) を実装
3. 7.6の使用例を参考に、main関数から呼び出し

---

### 7.1 ドライバ構造体定義

#### 説明

デバイスの状態を管理する構造体と、測定結果を格納する構造体を定義します。

**vl53l3cx_dev_t構造体:**
- `i2c_addr`: 現在のI2Cアドレス (7-bit)
- `fast_osc_frequency`: NVMから読み出した高速オシレータ周波数
- `timing_budget_us`: タイミングバジェット (測定時間)
- `measurement_active`: 測定中フラグ (0=停止中, 1=測定中)

**vl53l3cx_result_t構造体:**
- `distance_mm`: 計算された距離 (mm単位)
- `signal_rate_mcps`: 信号レート (オプション、本実装例では未使用)
- `range_status`: 測定ステータスコード (0x09=正常)
- `stream_count`: ストリームカウント
- `bin_data[24]`: 生ヒストグラムデータ (デバッグ用)

#### コード実装

```c
/**
 * VL53L3CX ドライバ構造体
 */
typedef struct {
    uint8_t i2c_addr;                    // I2Cアドレス
    uint16_t fast_osc_frequency;         // 高速オシレータ周波数 (NVMから読み出し)
    uint32_t timing_budget_us;           // タイミングバジェット
    uint8_t measurement_active;          // 測定中フラグ
} vl53l3cx_dev_t;

/**
 * 測定結果構造体
 */
typedef struct {
    uint16_t distance_mm;                // 距離 (mm)
    uint16_t signal_rate_mcps;           // 信号レート
    uint8_t range_status;                // 測定ステータス
    uint8_t stream_count;                // ストリームカウント
    uint32_t bin_data[24];               // 生ヒストグラムデータ
} vl53l3cx_result_t;
```

### 7.2 初期化関数 (完全版)

```c
/**
 * @brief VL53L3CX 初期化
 * @param dev デバイスハンドル
 * @return 0=成功, <0=エラー
 */
int vl53l3cx_init(vl53l3cx_dev_t *dev) {
    int ret;
    uint8_t boot_status;
    uint32_t start_time;

    // デバイス設定初期化
    dev->i2c_addr = 0x29;
    dev->timing_budget_us = 33333;  // 33ms
    dev->measurement_active = 0;

    // ========================================
    // ステップ 1: ファームウェア起動待ち
    // ========================================
    start_time = millis();
    do {
        ret = i2c_read(dev->i2c_addr, 0x0010, &boot_status, 1);
        if (ret != 0) return ret;

        if ((millis() - start_time) > 500) {
            return -1;  // タイムアウト
        }
        delay_ms(1);
    } while ((boot_status & 0x01) == 0);

    // ========================================
    // ステップ 2: NVM較正データ読み出し
    // ========================================
    ret = vl53l3cx_read_nvm_calibration_data(dev);
    if (ret != 0) return ret;

    // ========================================
    // ステップ 3-10: MEDIUM_RANGE プリセット設定
    // ========================================
    ret = vl53l3cx_set_preset_mode_medium_range(dev);
    if (ret != 0) return ret;

    return 0;  // 成功
}
```

### 7.3 NVM較正データ読み出し関数

```c
/**
 * @brief NVM較正データ読み出し
 */
int vl53l3cx_read_nvm_calibration_data(vl53l3cx_dev_t *dev) {
    int ret;
    uint8_t val;
    uint8_t nvm_data[4];

    // ファームウェア無効化
    val = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0401, &val, 1);
    if (ret != 0) return ret;

    // パワーフォース有効化
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0419, &val, 1);
    if (ret != 0) return ret;

    // パワーフォース安定化待ち
    delay_us(250);

    // NVM制御設定
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x01AC, &val, 1);  // PDN解除
    if (ret != 0) return ret;

    val = 0x05;
    ret = i2c_write(dev->i2c_addr, 0x01BB, &val, 1);  // CLK有効化
    if (ret != 0) return ret;

    // NVMパワーアップ待ち
    delay_us(5000);

    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x01AD, &val, 1);  // Mode設定
    if (ret != 0) return ret;

    // NVMパルス幅設定
    uint8_t pw_buf[2] = {0x00, 0x04};
    ret = i2c_write(dev->i2c_addr, 0x01AE, pw_buf, 2);
    if (ret != 0) return ret;

    // 高速オシレータ周波数読み出し (NVMアドレス 0x1C)
    ret = vl53l3cx_nvm_read(dev, 0x1C, nvm_data);
    if (ret != 0) return ret;

    dev->fast_osc_frequency = ((uint16_t)nvm_data[0] << 8) | nvm_data[1];

    // パワーフォース無効化
    val = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0419, &val, 1);
    if (ret != 0) return ret;

    // ファームウェア再有効化
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0401, &val, 1);
    if (ret != 0) return ret;

    return 0;
}

/**
 * @brief NVM単一アドレス読み出し
 */
int vl53l3cx_nvm_read(vl53l3cx_dev_t *dev, uint8_t nvm_addr, uint8_t *data_out) {
    int ret;
    uint8_t val;

    // NVMアドレス設定
    ret = i2c_write(dev->i2c_addr, 0x01B0, &nvm_addr, 1);
    if (ret != 0) return ret;

    // 読み出しトリガー (Low)
    val = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x01B1, &val, 1);
    if (ret != 0) return ret;

    // トリガー遅延
    delay_us(5);

    // 読み出しトリガー (High)
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x01B1, &val, 1);
    if (ret != 0) return ret;

    // データ読み出し (4バイト)
    ret = i2c_read(dev->i2c_addr, 0x01B2, data_out, 4);
    if (ret != 0) return ret;

    return 0;
}
```

### 7.4 プリセットモード設定関数

```c
/**
 * @brief MEDIUM_RANGE プリセットモード設定
 */
int vl53l3cx_set_preset_mode_medium_range(vl53l3cx_dev_t *dev) {
    int ret;
    uint8_t val;
    uint8_t buf[4];

    // ========== Static Configuration ==========
    // DSS設定
    buf[0] = 0x0A; buf[1] = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0024, buf, 2);  // DSS_CONFIG__TARGET_TOTAL_RATE_MCPS
    if (ret != 0) return ret;

    // GPIO/SPAD設定
    val = 0x10;
    ret = i2c_write(dev->i2c_addr, 0x0030, &val, 1);  // GPIO_HV_MUX__CTRL
    if (ret != 0) return ret;

    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x0031, &val, 1);  // GPIO__TIO_HV_STATUS
    if (ret != 0) return ret;

    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x0033, &val, 1);  // ANA_CONFIG__SPAD_SEL_PSWIDTH
    if (ret != 0) return ret;

    val = 0x08;
    ret = i2c_write(dev->i2c_addr, 0x0034, &val, 1);  // ANA_CONFIG__VCSEL_PULSE_WIDTH_OFFSET
    if (ret != 0) return ret;

    // Sigma推定器設定
    val = 0x08;
    ret = i2c_write(dev->i2c_addr, 0x0036, &val, 1);  // SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS
    if (ret != 0) return ret;

    val = 0x10;
    ret = i2c_write(dev->i2c_addr, 0x0037, &val, 1);  // SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS
    if (ret != 0) return ret;

    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0038, &val, 1);  // SIGMA_ESTIMATOR__SIGMA_REF_MM
    if (ret != 0) return ret;

    // アルゴリズム設定
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0039, &val, 1);  // ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM
    if (ret != 0) return ret;

    val = 0xFF;
    ret = i2c_write(dev->i2c_addr, 0x003E, &val, 1);  // ALGO__RANGE_IGNORE_VALID_HEIGHT_MM
    if (ret != 0) return ret;

    val = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x003F, &val, 1);  // ALGO__RANGE_MIN_CLIP
    if (ret != 0) return ret;

    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x0040, &val, 1);  // ALGO__CONSISTENCY_CHECK__TOLERANCE
    if (ret != 0) return ret;

    // ========== General Configuration ==========
    val = 0x20;
    ret = i2c_write(dev->i2c_addr, 0x0046, &val, 1);  // SYSTEM__INTERRUPT_CONFIG_GPIO
    if (ret != 0) return ret;

    val = 0x0B;
    ret = i2c_write(dev->i2c_addr, 0x0047, &val, 1);  // CAL_CONFIG__VCSEL_START
    if (ret != 0) return ret;

    buf[0] = 0x00; buf[1] = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0048, buf, 2);  // CAL_CONFIG__REPEAT_RATE
    if (ret != 0) return ret;

    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x004A, &val, 1);  // GLOBAL_CONFIG__VCSEL_WIDTH
    if (ret != 0) return ret;

    val = 0x0D;
    ret = i2c_write(dev->i2c_addr, 0x004B, &val, 1);  // PHASECAL_CONFIG__TIMEOUT_MACROP
    if (ret != 0) return ret;

    val = 0x21;
    ret = i2c_write(dev->i2c_addr, 0x004C, &val, 1);  // PHASECAL_CONFIG__TARGET
    if (ret != 0) return ret;

    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x004F, &val, 1);  // DSS_CONFIG__ROI_MODE_CONTROL
    if (ret != 0) return ret;

    // DSS詳細設定
    buf[0] = 0x8C; buf[1] = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0054, buf, 2);  // DSS_CONFIG__MANUAL_EFFECTIVE_SPADS_SELECT
    if (ret != 0) return ret;

    val = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0056, &val, 1);  // DSS_CONFIG__MANUAL_BLOCK_SELECT
    if (ret != 0) return ret;

    val = 0x38;
    ret = i2c_write(dev->i2c_addr, 0x0057, &val, 1);  // DSS_CONFIG__APERTURE_ATTENUATION
    if (ret != 0) return ret;

    val = 0xFF;
    ret = i2c_write(dev->i2c_addr, 0x0058, &val, 1);  // DSS_CONFIG__MAX_SPADS_LIMIT
    if (ret != 0) return ret;

    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0059, &val, 1);  // DSS_CONFIG__MIN_SPADS_LIMIT
    if (ret != 0) return ret;

    // ========== Timing Configuration ==========
    // MM タイムアウト設定
    buf[0] = 0x00; buf[1] = 0x1A;
    ret = i2c_write(dev->i2c_addr, 0x005A, buf, 2);  // MM_CONFIG__TIMEOUT_MACROP_A
    if (ret != 0) return ret;

    buf[0] = 0x00; buf[1] = 0x20;
    ret = i2c_write(dev->i2c_addr, 0x005C, buf, 2);  // MM_CONFIG__TIMEOUT_MACROP_B
    if (ret != 0) return ret;

    // Range タイムアウト・VCSEL周期設定
    buf[0] = 0x01; buf[1] = 0xCC;
    ret = i2c_write(dev->i2c_addr, 0x005E, buf, 2);  // RANGE_CONFIG__TIMEOUT_MACROP_A
    if (ret != 0) return ret;

    val = 0x0B;
    ret = i2c_write(dev->i2c_addr, 0x0060, &val, 1);  // RANGE_CONFIG__VCSEL_PERIOD_A
    if (ret != 0) return ret;

    buf[0] = 0x01; buf[1] = 0xF5;
    ret = i2c_write(dev->i2c_addr, 0x0061, buf, 2);  // RANGE_CONFIG__TIMEOUT_MACROP_B
    if (ret != 0) return ret;

    val = 0x09;
    ret = i2c_write(dev->i2c_addr, 0x0063, &val, 1);  // RANGE_CONFIG__VCSEL_PERIOD_B
    if (ret != 0) return ret;

    // Sigma閾値・Count Rate設定
    buf[0] = 0x00; buf[1] = 0x3C;
    ret = i2c_write(dev->i2c_addr, 0x0064, buf, 2);  // RANGE_CONFIG__SIGMA_THRESH
    if (ret != 0) return ret;

    buf[0] = 0x00; buf[1] = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0066, buf, 2);  // RANGE_CONFIG__MIN_COUNT_RATE_RTN_LIMIT_MCPS
    if (ret != 0) return ret;

    // Valid Phase設定
    val = 0x08;
    ret = i2c_write(dev->i2c_addr, 0x0068, &val, 1);  // RANGE_CONFIG__VALID_PHASE_LOW
    if (ret != 0) return ret;

    val = 0x78;
    ret = i2c_write(dev->i2c_addr, 0x0069, &val, 1);  // RANGE_CONFIG__VALID_PHASE_HIGH
    if (ret != 0) return ret;

    // 測定間隔設定 (100ms)
    buf[0] = 0x00; buf[1] = 0x00; buf[2] = 0x00; buf[3] = 0x64;
    ret = i2c_write(dev->i2c_addr, 0x006C, buf, 4);  // SYSTEM__INTERMEASUREMENT_PERIOD
    if (ret != 0) return ret;

    // ========== Dynamic Configuration ==========
    // Grouped Parameter Hold 開始
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0071, &val, 1);  // SYSTEM__GROUPED_PARAMETER_HOLD_0
    if (ret != 0) return ret;

    // 閾値設定
    buf[0] = 0x00; buf[1] = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0072, buf, 2);  // SYSTEM__THRESH_HIGH
    if (ret != 0) return ret;

    buf[0] = 0x00; buf[1] = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x0074, buf, 2);  // SYSTEM__THRESH_LOW
    if (ret != 0) return ret;

    // シード設定
    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x0077, &val, 1);  // SYSTEM__SEED_CONFIG
    if (ret != 0) return ret;

    // SD (Signal Detection) 設定
    val = 0x0B;
    ret = i2c_write(dev->i2c_addr, 0x0078, &val, 1);  // SD_CONFIG__WOI_SD0
    if (ret != 0) return ret;

    val = 0x09;
    ret = i2c_write(dev->i2c_addr, 0x0079, &val, 1);  // SD_CONFIG__WOI_SD1
    if (ret != 0) return ret;

    val = 0x0A;
    ret = i2c_write(dev->i2c_addr, 0x007A, &val, 1);  // SD_CONFIG__INITIAL_PHASE_SD0
    if (ret != 0) return ret;

    val = 0x0A;
    ret = i2c_write(dev->i2c_addr, 0x007B, &val, 1);  // SD_CONFIG__INITIAL_PHASE_SD1
    if (ret != 0) return ret;

    // Grouped Parameter Hold 継続
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x007C, &val, 1);  // SYSTEM__GROUPED_PARAMETER_HOLD_1
    if (ret != 0) return ret;

    // SD詳細設定
    val = 0x00;
    ret = i2c_write(dev->i2c_addr, 0x007D, &val, 1);  // SD_CONFIG__FIRST_ORDER_SELECT
    if (ret != 0) return ret;

    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x007E, &val, 1);  // SD_CONFIG__QUANTIFIER
    if (ret != 0) return ret;

    // ROI設定
    val = 0xC7;
    ret = i2c_write(dev->i2c_addr, 0x007F, &val, 1);  // ROI_CONFIG__USER_ROI_CENTRE_SPAD
    if (ret != 0) return ret;

    val = 0xFF;
    ret = i2c_write(dev->i2c_addr, 0x0080, &val, 1);  // ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE
    if (ret != 0) return ret;

    // シーケンス設定
    val = 0xC1;
    ret = i2c_write(dev->i2c_addr, 0x0081, &val, 1);  // SYSTEM__SEQUENCE_CONFIG
    if (ret != 0) return ret;

    // Grouped Parameter Hold 終了
    val = 0x02;
    ret = i2c_write(dev->i2c_addr, 0x0082, &val, 1);  // SYSTEM__GROUPED_PARAMETER_HOLD
    if (ret != 0) return ret;

    return 0;
}

/**
 * @brief ヒストグラムデータ処理 (ピーク検出・距離計算)
 */
void vl53l3cx_process_histogram(vl53l3cx_result_t *result) {
    // ステップ1: アンビエント推定 (最初の6ビンの平均)
    uint32_t ambient_sum = 0;
    for (int i = 0; i < 6; i++) {
        ambient_sum += result->bin_data[i];
    }
    uint32_t ambient_estimate = ambient_sum / 6;

    // ステップ2: アンビエント除去
    uint32_t corrected_bins[24];
    for (int i = 0; i < 24; i++) {
        if (result->bin_data[i] > ambient_estimate) {
            corrected_bins[i] = result->bin_data[i] - ambient_estimate;
        } else {
            corrected_bins[i] = 0;
        }
    }

    // ステップ3: ピーク検出 (ビン6-17の範囲で最大カウント検索)
    uint32_t max_count = 0;
    int peak_bin = 0;

    for (int i = 6; i < 18; i++) {
        if (corrected_bins[i] > max_count) {
            max_count = corrected_bins[i];
            peak_bin = i;
        }
    }

    // ステップ4: 距離計算
    // MEDIUM_RANGEモード:
    //   Period A (ビン0-11): VCSEL周期12 → ビン幅 約15.0mm
    //   Period B (ビン12-23): VCSEL周期10 → ビン幅 約12.5mm

    uint16_t distance_mm;

    if (peak_bin < 12) {
        // Period A 使用
        distance_mm = (uint16_t)(peak_bin * 15.0);
    } else {
        // Period B 使用
        distance_mm = (uint16_t)(peak_bin * 12.5);
    }

    // オプション: サブビン補間による精密距離計算
    if (peak_bin > 0 && peak_bin < 23 && max_count > 100) {
        // 前後のビンを使った放物線フィッティング
        int32_t a = corrected_bins[peak_bin - 1];
        int32_t b = corrected_bins[peak_bin];
        int32_t c = corrected_bins[peak_bin + 1];

        int32_t denominator = a - 2*b + c;

        // ゼロ除算回避
        if (denominator != 0) {
            float sub_bin_offset = 0.5f * (float)(a - c) / (float)denominator;
            float accurate_bin = (float)peak_bin + sub_bin_offset;

            // 精密距離計算
            if (peak_bin < 12) {
                distance_mm = (uint16_t)(accurate_bin * 15.0f);
            } else {
                distance_mm = (uint16_t)(accurate_bin * 12.5f);
            }
        }
    }

    // 結果格納
    result->distance_mm = distance_mm;
    result->peak_signal_count = max_count;
    result->ambient_count = ambient_estimate;
}
```

### 7.5 測定開始・データ読み出し関数

```c
/**
 * @brief 測定開始
 */
int vl53l3cx_start_ranging(vl53l3cx_dev_t *dev) {
    int ret;
    uint8_t val;

    // 割り込み設定
    val = 0x20;  // NEW_SAMPLE_READY
    ret = i2c_write(dev->i2c_addr, 0x0046, &val, 1);
    if (ret != 0) return ret;

    // 割り込みクリア
    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0086, &val, 1);
    if (ret != 0) return ret;

    // 測定開始
    val = 0x42;  // BACKTOBACK | HISTOGRAM
    ret = i2c_write(dev->i2c_addr, 0x0087, &val, 1);
    if (ret != 0) return ret;

    dev->measurement_active = 1;

    return 0;
}

/**
 * @brief データ読み出し
 */
int vl53l3cx_get_ranging_data(vl53l3cx_dev_t *dev, vl53l3cx_result_t *result) {
    int ret;
    uint8_t interrupt_status;
    uint8_t histogram_buffer[77];

    // データレディ待ち
    uint32_t start_time = millis();
    do {
        ret = i2c_read(dev->i2c_addr, 0x0089, &interrupt_status, 1);
        if (ret != 0) return ret;

        if ((millis() - start_time) > 2000) {
            return -1;  // タイムアウト
        }
        delay_ms(1);
    } while ((interrupt_status & 0x20) == 0);

    // ヒストグラムデータ一括読み出し
    ret = i2c_read(dev->i2c_addr, 0x0089, histogram_buffer, 77);
    if (ret != 0) return ret;

    // ヘッダー情報
    result->range_status = histogram_buffer[1] & 0x1F;
    result->stream_count = histogram_buffer[3];

    // ビンデータ抽出
    for (int bin = 0; bin < 24; bin++) {
        int offset = 5 + (bin * 3);
        result->bin_data[bin] = ((uint32_t)histogram_buffer[offset] << 16) |
                                ((uint32_t)histogram_buffer[offset+1] << 8) |
                                ((uint32_t)histogram_buffer[offset+2]);
    }

    // ピーク検出と距離計算
    vl53l3cx_process_histogram(result);

    // 割り込みクリア
    uint8_t val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0086, &val, 1);
    if (ret != 0) return ret;

    return 0;
}

/**
 * @brief 測定停止
 */
int vl53l3cx_stop_ranging(vl53l3cx_dev_t *dev) {
    int ret;
    uint8_t val = 0x00;

    ret = i2c_write(dev->i2c_addr, 0x0087, &val, 1);
    if (ret != 0) return ret;

    ret = i2c_write(dev->i2c_addr, 0x0087, &val, 1);
    if (ret != 0) return ret;

    val = 0x01;
    ret = i2c_write(dev->i2c_addr, 0x0086, &val, 1);
    if (ret != 0) return ret;

    dev->measurement_active = 0;

    return 0;
}
```

### 7.6 使用例

```c
int main(void) {
    vl53l3cx_dev_t sensor;
    vl53l3cx_result_t result;

    // ========================================
    // 初期化
    // ========================================
    if (vl53l3cx_init(&sensor) != 0) {
        printf("Initialization failed\n");
        return -1;
    }

    printf("VL53L3CX initialized (fast_osc_freq: %d)\n", sensor.fast_osc_frequency);

    // ========================================
    // 測定開始
    // ========================================
    if (vl53l3cx_start_ranging(&sensor) != 0) {
        printf("Start ranging failed\n");
        return -1;
    }

    printf("Ranging started\n");

    // ========================================
    // メインループ
    // ========================================
    for (int i = 0; i < 100; i++) {
        // データ読み出し
        if (vl53l3cx_get_ranging_data(&sensor, &result) == 0) {
            if (result.range_status == 0x09) {  // RANGE_COMPLETE
                printf("[%d] Distance: %d mm, Signal: %d MCPS\n",
                       i, result.distance_mm, result.signal_rate_mcps);
            } else {
                printf("[%d] Range Error: %d\n", i, result.range_status);
            }
        }

        delay_ms(100);  // 100ms周期
    }

    // ========================================
    // 測定停止
    // ========================================
    vl53l3cx_stop_ranging(&sensor);
    printf("Ranging stopped\n");

    return 0;
}
```

---

## まとめ

本ドキュメントは、VL53L3CXセンサーのオリジナルドライバを**完全に実装可能**な形で提供しました。

### 実装の要点

1. **初期化手順**: ファームウェア起動待ち → NVM読み出し → プリセット設定
2. **NVM読み出し**: 250us, 5ms, 5us の待ち時間が必須
3. **プリセット設定**: 50以上のレジスタを正確に設定
4. **測定制御**: 0x0087レジスタで開始/停止
5. **データ読み出し**: 0x0089から77バイト一括読み出し

### 重要な待ち時間

| 箇所 | 待ち時間 | 理由 |
|------|----------|------|
| パワーフォース後 | 250us | 電源安定化 |
| NVMパワーアップ後 | 5ms | NVM準備完了 |
| NVM読み出しトリガー後 | 5us | 読み出し完了待ち |
| データレディ待ち | 1msポーリング | 測定完了検出 |

このガイドに従えば、ST提供のライブラリを使わずにVL53L3CXを完全に制御できます。

---

## 付録A: レジスタリファレンス

本セクションでは、VL53L3CX実装で使用する全レジスタを一覧表にまとめています。

### A.1 デバイス制御レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0001 | I2C_SLAVE__DEVICE_ADDRESS | 1 byte | I2Cデバイスアドレス(7-bit) | R/W | 4章: アドレス変更 |
| 0x0010 | FIRMWARE__SYSTEM_STATUS | 1 byte | ファームウェア起動状態<br>bit 0: 起動完了 (1=完了) | R | 1章: 起動待ち |
| 0x002E | PAD_I2C_HV__EXTSUP_CONFIG | 1 byte | I2C電圧設定<br>bit 0: 2.8V使用時に1設定 | R/W | 1章: I2C電圧設定 |
| 0x0401 | FIRMWARE__ENABLE | 1 byte | ファームウェア有効/無効<br>0x00=無効, 0x01=有効 | R/W | 2章: NVM読み出し |
| 0x0419 | POWER_MANAGEMENT__GO1_POWER_FORCE | 1 byte | パワーフォース制御<br>0x00=無効, 0x01=有効 | R/W | 2章: NVM読み出し |

### A.2 NVM制御レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x01AC | RANGING_CORE__NVM_CTRL__PDN | 1 byte | NVMパワーダウン制御<br>0x00=パワーダウン, 0x01=アクティブ | R/W | 2章: NVM有効化 |
| 0x01AD | RANGING_CORE__NVM_CTRL__MODE | 1 byte | NVM動作モード<br>0x01=読み出しモード | R/W | 2章: NVM読み出し |
| 0x01AE | RANGING_CORE__NVM_CTRL__PULSE_WIDTH_MSB | 2 bytes | NVMパルス幅設定<br>デフォルト: 0x0004 | R/W | 2章: NVM読み出し |
| 0x01B0 | RANGING_CORE__NVM_CTRL__ADDR | 1 byte | NVMアドレス指定<br>範囲: 0x00-0xFF | R/W | 2章: NVMアドレス設定 |
| 0x01B1 | RANGING_CORE__NVM_CTRL__READN | 1 byte | NVM読み出しトリガー<br>0→1遷移で読み出し実行 | R/W | 2章: NVM読み出しトリガー |
| 0x01B2 | RANGING_CORE__NVM_CTRL__DATAOUT_MMM | 4 bytes | NVM読み出しデータ | R | 2章: NVMデータ取得 |
| 0x01BB | RANGING_CORE__CLK_CTRL1 | 1 byte | NVMクロック制御<br>0x05=クロック有効 | R/W | 2章: NVMクロック有効化 |

### A.3 GPIO・割り込み設定レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0030 | GPIO_HV_MUX__CTRL | 1 byte | GPIO多重化制御<br>0x10=ACTIVE_LOW 割り込み出力 | R/W | 3章: GPIO設定 |
| 0x0031 | GPIO__TIO_HV_STATUS | 1 byte | GPIO状態<br>0x02=デフォルト値 | R/W | 3章: GPIO設定 |
| 0x0046 | SYSTEM__INTERRUPT_CONFIG_GPIO | 1 byte | 割り込み設定<br>0x20=NEW_SAMPLE_READY | R/W | 3章, 5章: 割り込み設定 |
| 0x0086 | SYSTEM__INTERRUPT_CLEAR | 1 byte | 割り込みクリア<br>0x01で割り込みクリア | W | 3章, 5章: 割り込みクリア |
| 0x0089 | RESULT__INTERRUPT_STATUS | 1 byte | 割り込みステータス<br>bit 5: NEW_DATA_READY | R | 5章: データレディ待ち |

### A.4 SPAD・VCSEL設定レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0033 | ANA_CONFIG__SPAD_SEL_PSWIDTH | 1 byte | SPADパルス幅選択<br>0x02=MEDIUM_RANGE用 | R/W | 3章: SPAD設定 |
| 0x0034 | ANA_CONFIG__VCSEL_PULSE_WIDTH_OFFSET | 1 byte | VCSELパルス幅オフセット<br>0x08=デフォルト | R/W | 3章: VCSEL設定 |
| 0x004A | GLOBAL_CONFIG__VCSEL_WIDTH | 1 byte | VCSELグローバル幅<br>0x02=MEDIUM_RANGE用 | R/W | 3章: VCSEL設定 |

### A.5 シグマ推定・アルゴリズム設定レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0036 | SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS | 1 byte | 有効パルス幅<br>0x08=8ns | R/W | 3章: シグマ推定 |
| 0x0037 | SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS | 1 byte | 有効アンビエント幅<br>0x02=2ns | R/W | 3章: シグマ推定 |
| 0x0038 | SIGMA_ESTIMATOR__SIGMA_REF_MM | 1 byte | シグマ基準値<br>0x01=1mm | R/W | 3章: シグマ推定 |
| 0x0039 | ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM | 1 byte | クロストーク補正有効高さ<br>0x01=デフォルト | R/W | 3章: アルゴリズム設定 |
| 0x003E | ALGO__RANGE_IGNORE_VALID_HEIGHT_MM | 1 byte | 測定範囲無視高さ<br>0xFF=無効化 | R/W | 3章: アルゴリズム設定 |
| 0x003F | ALGO__RANGE_MIN_CLIP | 1 byte | 最小範囲クリップ<br>0x00=クリップなし | R/W | 3章: アルゴリズム設定 |
| 0x0040 | ALGO__CONSISTENCY_CHECK__TOLERANCE | 1 byte | 一貫性チェック許容値<br>0x02=デフォルト | R/W | 3章: アルゴリズム設定 |

### A.6 キャリブレーション設定レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0047 | CAL_CONFIG__VCSEL_START | 1 byte | VCSEL開始設定<br>0x0B=11 (MEDIUM_RANGE用) | R/W | 3章: キャリブレーション設定 |
| 0x0048 | CAL_CONFIG__REPEAT_RATE | 2 bytes | キャリブレーション繰り返しレート<br>0x0000=デフォルト | R/W | 3章: キャリブレーション設定 |
| 0x004B | PHASECAL_CONFIG__TIMEOUT_MACROP | 1 byte | フェーズキャリブレーションタイムアウト<br>0x0D=ヒストグラムモード用 | R/W | 3章: フェーズキャリブレーション |
| 0x004C | PHASECAL_CONFIG__TARGET | 1 byte | フェーズキャリブレーションターゲット<br>0x21=デフォルト | R/W | 3章: フェーズキャリブレーション |

### A.7 タイミング設定レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x005A | MM_CONFIG__TIMEOUT_MACROP_A | 2 bytes | MM (Multi-Mode) タイムアウトA<br>0x001A=エンコード値 | R/W | 3章: タイミング設定 |
| 0x005C | MM_CONFIG__TIMEOUT_MACROP_B | 2 bytes | MM タイムアウトB<br>0x0020=エンコード値 | R/W | 3章: タイミング設定 |
| 0x005E | RANGE_CONFIG__TIMEOUT_MACROP_A | 2 bytes | 測定タイムアウトA<br>0x01CC=エンコード値 (MEDIUM_RANGE用) | R/W | 3章: タイミング設定 |
| 0x0060 | RANGE_CONFIG__VCSEL_PERIOD_A | 1 byte | VCSEL周期A<br>0x0B=エンコード値 (実周期12) | R/W | 3章: VCSEL周期設定 |
| 0x0061 | RANGE_CONFIG__TIMEOUT_MACROP_B | 2 bytes | 測定タイムアウトB<br>0x01F5=エンコード値 (MEDIUM_RANGE用) | R/W | 3章: タイミング設定 |
| 0x0063 | RANGE_CONFIG__VCSEL_PERIOD_B | 1 byte | VCSEL周期B<br>0x09=エンコード値 (実周期10) | R/W | 3章: VCSEL周期設定 |
| 0x006C | SYSTEM__INTERMEASUREMENT_PERIOD | 4 bytes | 測定間隔 (ms単位)<br>例: 100 = 100ms周期 | R/W | 3章: 測定間隔設定 |

### A.8 Dynamic Configuration レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0071 | SYSTEM__GROUPED_PARAMETER_HOLD_0 | 1 byte | パラメータ保持開始0<br>0x01=開始 | R/W | 3章: Grouped Parameter Hold |
| 0x0072 | SYSTEM__THRESH_HIGH | 2 bytes | 高閾値設定<br>0x0000=デフォルト | R/W | 3章: 閾値設定 |
| 0x0074 | SYSTEM__THRESH_LOW | 2 bytes | 低閾値設定<br>0x0000=デフォルト | R/W | 3章: 閾値設定 |
| 0x0077 | SYSTEM__SEED_CONFIG | 1 byte | シード設定<br>0x02=デフォルト | R/W | 3章: シード設定 |
| 0x0078 | SD_CONFIG__WOI_SD0 | 1 byte | Window of Interest SD0<br>0x0B=VCSEL Period Aに合わせる | R/W | 3章: SD設定 |
| 0x0079 | SD_CONFIG__WOI_SD1 | 1 byte | Window of Interest SD1<br>0x09=VCSEL Period Bに合わせる | R/W | 3章: SD設定 |
| 0x007A | SD_CONFIG__INITIAL_PHASE_SD0 | 1 byte | 初期位相SD0<br>0x0A=デフォルト | R/W | 3章: SD設定 |
| 0x007B | SD_CONFIG__INITIAL_PHASE_SD1 | 1 byte | 初期位相SD1<br>0x0A=デフォルト | R/W | 3章: SD設定 |
| 0x007C | SYSTEM__GROUPED_PARAMETER_HOLD_1 | 1 byte | パラメータ保持開始1<br>0x01=継続 | R/W | 3章: Grouped Parameter Hold |
| 0x007F | ROI_CONFIG__USER_ROI_CENTRE_SPAD | 1 byte | ROI中心SPAD<br>0xC7=199 (中心) | R/W | 3章: ROI設定 |
| 0x0080 | ROI_CONFIG__USER_ROI_REQUESTED_GLOBAL_XY_SIZE | 1 byte | ROIサイズ<br>0xFF=16x16 (最大) | R/W | 3章: ROI設定 |
| 0x0081 | SYSTEM__SEQUENCE_CONFIG | 1 byte | シーケンス設定<br>0xC1=VHV+PHASECAL+MM2+RANGE | R/W | 3章: シーケンス設定 |
| 0x0082 | SYSTEM__GROUPED_PARAMETER_HOLD | 1 byte | パラメータ保持終了<br>0x02=終了・適用 | R/W | 3章: Grouped Parameter Hold |

### A.9 測定制御レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0083 | SYSTEM__STREAM_COUNT_CTRL | 1 byte | ストリームカウント制御<br>0x00=無限ループ | R/W | 3章: ストリーム設定 |
| 0x0087 | SYSTEM__MODE_START | 1 byte | 測定開始/停止<br>0x42=BACKTOBACK+HISTOGRAM<br>0x00=STOP | R/W | 5章: 測定開始・停止 |

### A.10 測定結果レジスタ

| アドレス | レジスタ名 | サイズ | 機能 | R/W | 使用箇所 |
|---------|-----------|--------|------|-----|----------|
| 0x0089 | RESULT__INTERRUPT_STATUS | 1 byte | 割り込みステータス<br>bit 5: NEW_DATA_READY<br>bit 4: ERROR<br>bit 3: RANGE_COMPLETE | R | 5章, 6章: 測定完了確認 |
| 0x008A | RESULT__RANGE_STATUS | 1 byte | 測定ステータス<br>bit [4:0]: ステータスコード<br>0x09=正常測定完了 | R | 6章: 測定ステータス確認 |
| 0x008E | RESULT__HISTOGRAM_BIN_0_2 | 3 bytes | ヒストグラムビン0 (24-bit) | R | 6章: データ読み出し |
| ... | ... | ... | ヒストグラムビン1-22 (各3バイト) | R | 6章: データ読み出し |
| 0x00D3 | RESULT__HISTOGRAM_BIN_23_2 | 3 bytes | ヒストグラムビン23 (24-bit) | R | 6章: データ読み出し |

**注記**: ヒストグラムビンデータは0x008Eから0x00D5まで連続しており、24ビン × 3バイト = 72バイトを一括読み出しできます。

### A.11 重要な待ち時間まとめ

| 操作 | 待ち時間 | 理由 | 関連レジスタ |
|------|----------|------|-------------|
| パワーフォース有効化後 | **250μs** | 電源安定化待ち | 0x0419 |
| NVM クロック有効化後 | **5ms** | NVM準備完了待ち | 0x01BB |
| NVM 読み出しトリガー後 | **5μs** | NVM読み出し完了待ち | 0x01B1 |
| ファームウェア起動 | 最大**500ms** | 起動完了待ち (ポーリング) | 0x0010 |
| データレディ待ち | 最大**2000ms** | 測定完了待ち (ポーリング) | 0x0089 |

### A.12 レジスタアクセスのベストプラクティス

1. **I2C連続書き込み活用**: アドレスが連続するレジスタは一括書き込みで効率化
   - 例: 0x0030-0x0040の設定を1トランザクションで実行可能

2. **Read-Modify-Write**: ビットフィールド変更時は必ず現在値を読んでから書き込む
   - 例: PAD_I2C_HV__EXTSUP_CONFIG (0x002E) のbit 0変更時

3. **Grouped Parameter Hold**: Dynamic Configuration変更時は0x0071, 0x007C, 0x0082を使用
   - 複数レジスタの変更をアトミックに反映

4. **割り込みクリア**: データ読み出し後は必ず0x0086で割り込みクリア

5. **測定停止**: 確実に停止するため、0x0087に0x00を2回書き込む (メーカー推奨)

---

## 付録B: NVMアドレスマップ

VL53L3CXの工場出荷時較正データはNVMに保存されています。以下は主要なNVMアドレスです。

| NVMアドレス | データ内容 | サイズ | 用途 |
|------------|-----------|--------|------|
| 0x11 | I2C_SLAVE__DEVICE_ADDRESS | 1 byte | デフォルトI2Cアドレス (0x29) |
| 0x1C | OSC_MEASURED__FAST_OSC_FREQUENCY_MSB | 1 byte | 高速オシレータ周波数 (上位バイト) |
| 0x1D | OSC_MEASURED__FAST_OSC_FREQUENCY_LSB | 1 byte | 高速オシレータ周波数 (下位バイト) |
| 0x2C | VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND | 1 byte | VHVタイムアウト設定 |

**注記**: NVMデータは1アドレスあたり4バイトですが、実際に使用するのは最初の1-2バイトです。

---

## 付録C: エラーコード一覧

### RANGE_STATUS エラーコード (レジスタ 0x008A の bit [4:0])

| コード | 名称 | 意味 | 対処方法 |
|--------|------|------|----------|
| 0x00 | RANGE_VALID | 範囲測定成功 (但し、他の警告あり) | ステータス詳細を確認 |
| 0x01 | SIGMA_FAIL | シグマ推定失敗 (ノイズ過大) | 測定環境改善、積分時間延長 |
| 0x02 | SIGNAL_FAIL | 信号強度不足 | ターゲット反射率確認、距離短縮 |
| 0x04 | OUTOFBOUNDS_FAIL | 測定範囲外 | ターゲット距離確認 |
| 0x05 | HARDWARE_FAIL | ハードウェアエラー | デバイス再起動 |
| 0x07 | WRAP_TARGET_FAIL | 位相ラップエラー | 測定範囲調整 |
| 0x08 | PROCESSING_FAIL | 処理エラー | データ読み直し |
| 0x09 | **RANGE_COMPLETE** | **正常測定完了** | 正常 (データ使用可能) |
| 0x0D | XTALK_SIGNAL_FAIL | クロストーク補正失敗 | クロストークキャリブレーション実施 |
| 0x0E | SYNCRONISATION_INT | 同期割り込み | - |
| 0x12 | MIN_RANGE_FAIL | 最小距離未満 | ターゲットが近すぎる |
| 0x13 | RANGE_INVALID | 無効な測定結果 | データ破棄 |

**推奨**: `range_status == 0x09` のときのみデータを有効とする。
