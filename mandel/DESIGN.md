# Mandelbrot on the RSP — design notes

Goal: compute Mandelbrot escape iterations on the RSP vector unit with libdragon,
raw ucode (no rspq) so we learn the metal. Ares = debugger of record.

## Why RSP is a good fit
- Escape-time loop is branch-free per pixel → pure SIMD lane work.
- 8×32-bit lanes (or 16×16-bit) per vector instruction.
- The scalar side is just RDRAM<->DMEM DMA plumbing.

## Fixed-point scheme (RSP has NO floating point)
q12 per component, packed two-complex-per-lane:
  lane32 = [ x(q12,s16) | y(q12,s16) ]   range [-8, +8) — covers set with margin
c per column: x0 = -2.5 + i*dx; pack(x0,y0) as one 32-bit word per column.
q12 chosen so z² stays in s32 when |c|,|z| < 8 (we clamp earlier at escape 2).

## Ucode contract (mandel_vec.S)
  rsp_run(rl, wl) with a0=RDRAM results ptr (64B rows), a1=RDRAM c ptr
  DMEM layout:
    0x0000 params: [0]=cols(8) [1]=maxiter [4]=row_ptr [8]=c_ptr
    0x0040 c row: 8× u32 packed q12 (CPU DMA'd before run)
    0x0200 results: 8× u32 (iteration counts packed as 8×u8? -> keep u32 row for now)
  Ucode: loads nothing itself on first pass (CPU loads params+c via rsp_load_data),
  computes one row of 8 columns × 16 lanes?? -> v0: keep 8 pixels per run,
  CPU drives 320/8 = 40 runs per row line... too many; tile to 8 cols x full rows.

## Math per iteration (all on v2 = z, v3 = c, both 8×32 packed [x|y])
  # z² — we compute real/imag halves separately (each q12 squared -> q24 in 32-bit)
  VMULH v2.x*v2.x, VMULH v2.y*v2.y     # VMULH: (a*b)>>16, q12*q12=q24
  re = vmulh(x,x) - vmulh(y,y)         # real part of z² (q24)
  im = vmulh(x,y)                       # x*y is already half of 2xy; double it:
       # 2*vmulh(x,y) via VADD im+im (one extra add, cheap)
  # re2 += c.re : c is q12 -> shift to q24: c<<4 ; likewise im
  # escape test: re*re + im*im > 4<<8 (q16 units after >>16)... 
  # simpler: test |re|,|im| via VGE on q24 halves vs (2<<16)... 
  # (exact shift bookkeeping is the classic RSP first-bug — verify in Ares disasm!)
  clamp: VMAX/VMIN ±0x7FFF on q16 result after >>8 foldback
  counter: VADD cnt, cnt, one; freeze escaped lanes VAND trick

## Hazards we must honor (RSP Programmer's Guide ch. on VU timing)
- VU multiply result latency = 2 cycles: interleave independent lane work or
  insert NOP between dependent multiplies (classic first bug).
- Loads: 2-cycle latency; scalar from vector (VMFC*) = 3+ cycles.
- Store after load of same address needs sync.

## Test loop
  make -> mandel.z64; tools/n64test_ares.sh mandel.z64 30  (vision: fractal rows appear)
  Ares: Developer/HomebrewMode + RSP debugger for single-stepping VMULH chain.

## Roadmap
1. CPU q12 Mandelbrot (validate coord mapping + framebuffer blit)
2. RSP ucode port, one 8-px row per run, results via rsp_read_data
3. Tile 8x8, ucode does full tile per run, 150 tiles/frame -> smooth paint
4. 16-lane q15 packing for 2× throughput
