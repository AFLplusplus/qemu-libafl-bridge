# syx snapshot (systemmode only)
subdir('syx-snapshot')

# generic
specific_ss.add(files(
  'cpu.c',
  'exit.c',
  'hook.c',
  'jit.c',
  'utils.c',
  'gdb.c',
  'tcg.c',
  'tcg-helper.c',

  # TCG-related hooks
  'hooks/tcg/backdoor.c',
  'hooks/tcg/block.c',
  'hooks/tcg/cmp.c',
  'hooks/tcg/edge.c',
  'hooks/tcg/instruction.c',
  'hooks/tcg/read_write.c',

  # General hooks
  'hooks/cpu_run.c',
  'hooks/thread.c',
))

# usermode specific
specific_ss.add(when : 'CONFIG_USER_ONLY', if_true: [files(
  'user.c',
  'hooks/syscall.c',
)])

# systemmode specific
specific_ss.add(when : 'CONFIG_USER_ONLY', if_false: [files(
  'system.c',
  'qemu_snapshot.c',
)])

#### --- Begin AFL++ code ---
if 'CONFIG_AFL' in config_host
  specific_ss.add(when : 'CONFIG_USER_ONLY', if_true: [files(
    '../../libaflqemubridge/afl_setup.c',
    '../../libaflqemubridge/afl_idtable.c',
    '../../libaflqemubridge/afl_coverage.c',
    '../../libaflqemubridge/afl_forkserver.c',
    '../../libaflqemubridge/afl_ranges.c',
    '../../libaflqemubridge/afl_persistent.c',
    '../../libaflqemubridge/afl_compcov.c',
    '../../libaflqemubridge/afl_cmplog.c',
    '../../libaflqemubridge/afl_qasan.c',
    '../../libaflqemubridge/afl_ijon.c',
    '../../libaflqemubridge/afl_tsl.c',
  )])
endif
#### --- End AFL++ code ---

