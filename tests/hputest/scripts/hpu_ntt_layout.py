"""新版手册的 NTT 物理 ABI：loader、P 网络和 lazy-scale twiddle 校验模型。

此模块只在 host 导入/回归中使用，不生成替代 producer 资产或指令。
"""

CONVENTION = "hardware loader batch/lane order with P after PNTT and P^-1 before PINTT"
HARDWARE_LAYOUT = "coefficient domain bit-reversed, NTT domain P-network physical"


def bit_reverse(index, n):
    return int(f"{index:0{n.bit_length() - 1}b}"[::-1], 2)


def batch_positions(n, stage):
    """每项列出 loader 的 128 个 PE 寄存器对应的物理字地址。"""
    half = 1 << stage
    if half < 128:
        for base in range(0, n, 128):
            yield list(range(base, base + 128))
    else:
        for group in range(0, n, 2 * half):
            for offset in range(0, half, 64):
                yield [group + offset + lane + side * half
                       for lane in range(64) for side in range(2)]


def p_network(values, inverse=False):
    """P 将相邻偶/奇 lane 分到前/后 64 字；P^-1 反向交织。"""
    if inverse:
        return [values[lane + side * 64] for lane in range(64) for side in range(2)]
    return values[::2] + values[1::2]


def forward_layout(n):
    labels = list(range(n))
    for stage in range(n.bit_length() - 1):
        for positions in batch_positions(n, stage):
            changed = p_network([labels[p] for p in positions])
            for position, label in zip(positions, changed):
                labels[position] = label
    return labels


def physical_words(logical, ntt_domain=False):
    n = len(logical)
    layout = forward_layout(n) if ntt_domain else [bit_reverse(i, n) for i in range(n)]
    return [logical[index] for index in layout]


def expected_twiddles(n, q, psi):
    """独立按物理 label 和比例标记推导表；拒绝旧 group-major/逆根表。"""
    omega = psi * psi % q
    labels = list(range(n))
    forward = []
    for stage in range(n.bit_length() - 1):
        half, table = 1 << stage, []
        for positions in batch_positions(n, stage):
            batch = [labels[p] for p in positions]
            for low, high in zip(batch[::2], batch[1::2]):
                if high != low + half:
                    raise ValueError("forward physical lane pairing mismatch")
                table.append(pow(omega, (low % half) * n // (2 * half), q))
            for position, label in zip(positions, p_network(batch)):
                labels[position] = label
        forward.append(table)

    scales = [1] * n
    inverse = []
    for stage in reversed(range(n.bit_length() - 1)):
        half, table = 1 << stage, []
        for positions in batch_positions(n, stage):
            batch = p_network([labels[p] for p in positions], inverse=True)
            scale = p_network([scales[p] for p in positions], inverse=True)
            for even in range(0, 128, 2):
                low, high = batch[even:even + 2]
                if high != low + half:
                    raise ValueError("inverse physical lane pairing mismatch")
                alpha, beta = scale[even:even + 2]
                table.append(alpha * pow(beta, -1, q) % q)
                w = pow(omega, (low % half) * n // (2 * half), q)
                scale[even + 1] = alpha * w % q
            for position, label, value in zip(positions, batch, scale):
                labels[position], scales[position] = label, value
        inverse.append(table)
    if labels != list(range(n)) or scales != [1] * n:
        raise ValueError("inverse schedule did not restore labels/scales")
    return {"ntt": forward, "intt": inverse}


def stage_reference(inputs, twiddles, q, stage, inverse=False):
    n = len(inputs)
    forward_stage = n.bit_length() - 2 - stage if inverse else stage
    output, cursor = list(inputs), 0
    for positions in batch_positions(n, forward_stage):
        batch = [inputs[p] for p in positions]
        if inverse:
            batch = p_network(batch, inverse=True)
        for even in range(0, 128, 2):
            a, b = batch[even], batch[even + 1] * twiddles[cursor] % q
            batch[even], batch[even + 1] = (a + b) % q, (a - b) % q
            cursor += 1
        if not inverse:
            batch = p_network(batch)
        for position, value in zip(positions, batch):
            output[position] = value
    if cursor != n // 2 or len(twiddles) != cursor:
        raise ValueError("stage must consume exactly N/2 twiddles")
    return output
