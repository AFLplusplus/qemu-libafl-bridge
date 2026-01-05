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

