specific_ss.add(when : 'CONFIG_USER_ONLY', if_false : [files(
  'device-save.c',
  'syx-snapshot.c',
  'syx-cow-cache.c',
  'channel-buffer-writeback.c',
)])

# placeholder for qemu-img, etc.
util_ss.add(files(
  'syx-snapshot-wrap.c'
))
