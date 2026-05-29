# BLE 命令协议参考

## 概述

所有命令通过 BLE 特征值 (UUID: `00002446-0000-1000-8000-00805F9B34FB`) 传输。

- 命令格式：`[2 字节命令码 (大端)] + [可选数据]`
- 响应格式：`[状态字节] + [命令码低字节] + [数据]`
  - `0x00` = 成功
  - `0xFF` = 错误
- MTU：512 字节

## 命令列表

### 0x000F - 重启设备

**请求**：`[0x00, 0x0F]`

设备立即重启。

---

### 0x0043 - 固件版本查询

**请求**：`[0x00, 0x43]`

**响应**：
```
[0x00] [0x43] [major] [minor] [sha_len] [sha_hex...]
```
- `major`: 主版本号（当前为 0）
- `minor`: 次版本号（当前为 2）
- `sha_len`: Git SHA 十六进制字符串长度（固定 40）
- `sha_hex`: Git SHA 十六进制字符串

---

### 0x0044 - 读取设备状态 (MSD)

**请求**：`[0x00, 0x44]`

**响应**：
```
[0x00] [0x44] [msd_payload_16_bytes]
```

MSD 结构 (16 字节)：

| 偏移 | 长度 | 说明 |
|------|------|------|
| 0-1 | 2 | Company ID (0x2446) |
| 2-12 | 11 | 动态状态数据 |
| 13 | 1 | 温度编码值：`temp_C = byte / 2.0 - 40.0` |
| 14 | 1 | 电池电压低字节 |
| 15 | 1 | 状态字节（bit0: 电压高位, bit1: 重启标志, bit2: 连接请求, bit4-7: 循环计数） |

电池电压解码：
```
voltage_10mv = (status_byte & 0x01) << 8 | voltage_low_byte
voltage_V = voltage_10mv * 10 / 1000.0
```

---

### 0x0070 - 直接写入开始（位图模式）

**请求**：`[0x00, 0x70]`

**响应**：`[0x00] [0x70]`（成功）

开始接收黑白红双平面位图数据。接收顺序：先黑平面 (15KB)，后红平面 (15KB)。

---

### 0x0071 - 直接写入数据（位图模式）

**请求**：`[0x00, 0x71] + [image_data]`

**响应**：
- 当前平面未收满：`[0x00] [0x71]`
- 当前平面收满：`[0x00] [0x72]`（自动切到下一平面或触发刷新）

---

### 0x0072 - 直接写入结束（位图模式）

**请求**：`[0x00, 0x72]`

当红平面收满时自动触发，无需主机手动发送。

**响应**：
- ACK：`[0x00] [0x72]`
- 刷新成功：`[0x00] [0x73]`
- 刷新超时：`[0x00] [0x74]`

---

### 0x0078 - 看板渲染开始（结构化模式）

**请求**：
```
[0x00, 0x78]
[version: 1]         // 固定 1
[flags: 1]           // bit0: FULL, bit1: FAST
[payload_len_le: 2]  // 固定 192
[crc32_le: 4]        // 完整 payload 的 CRC32
```

**响应**：`[0x00] [0x78]`（成功）或 `[0xFF] [0x78] [error] [0x00]`（失败）

---

### 0x0079 - 看板渲染数据（结构化模式）

**请求**：`[0x00, 0x79] + [payload_chunk]`

**响应**：`[0x00] [0x79]`（每块 ACK）

---

### 0x007A - 看板渲染提交（结构化模式）

**请求**：
```
[0x00, 0x7A]
[refresh_mode: 1]    // 0=FULL, 1=FAST
```

**响应**：
- ACK：`[0x00] [0x7A]`
- 渲染成功：`[0x00] [0x7B]`
- 渲染失败：`[0x00] [0x7C]`

---

## 看板协议错误码

| 错误码 | 含义 |
|--------|------|
| `0x01` | 版本不支持 |
| `0x02` | payload 长度非法 |
| `0x04` | CRC 校验失败 |
| `0x05` | 状态机错误 |
| `0x07` | 设备忙（正在渲染） |
| `0x08` | 刷新模式不支持 |

## 看板结构化快照格式 (DashboardSnapshotV1)

固定 192 字节，little-endian：

```text
offset  size  field
0       1     schema_version
1       1     row_count
2       1     glm_5h_percent
3       1     glm_week_percent
4       1     gpt_5h_percent
5       1     gpt_week_percent
6       1     glm_level_len
7       1     reserved
8       4     total_tokens_u32
12      4     input_tokens_u32
16      4     output_tokens_u32
20      4     cache_tokens_u32
24      5     last_refresh_ascii
29      5     glm_5h_label_ascii
34      5     glm_week_label_ascii
39      5     gpt_5h_label_ascii
44      5     gpt_week_label_ascii
49      8     glm_level_ascii
57      132   model_rows[4]
189     3     reserved (含时间同步数据)
```

模型行 (每行 33 字节)：

```text
offset  size  field
0       24    model_ascii
24      1     provider_code (1=zhipu, 2=openai, 3=anthropic, 4=google, 255=other)
25      2     calls_u16
27      4     total_tokens_u32
31      2     share_bp_u16 (百分比基点，100.00% = 10000)
```

## 协议状态机

```
IDLE ──START(0x0078)──> RECEIVING
RECEIVING ──收满 payload──> READY_TO_COMMIT
READY_TO_COMMIT ──COMMIT(0x007A)──> RENDERING
RENDERING ──成功/失败──> IDLE
```
