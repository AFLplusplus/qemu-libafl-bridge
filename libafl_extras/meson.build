specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files(
  'syx-snapshot/device-save.c',
  'syx-snapshot/syx-snapshot.c',
  'syx-snapshot/syx-cow-cache.c',
  'syx-snapshot/channel-buffer-writeback.c',
)])
specific_ss.add(files('exit.c', 'hook.c', 'jit.c'))
