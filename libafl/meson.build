libafl_ss.add(files(
                    'cpu.c',
                    'exit.c',
                    'hook.c',
                    'jit.c',
                    'utils.c',
                    'gdb.c',

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

libafl_ss.add(when : 'CONFIG_SOFTMMU', if_true : [files(
                                                        'system.c',
                                                        'qemu_snapshot.c',
                                                        'syx-snapshot/device-save.c',
                                                        'syx-snapshot/syx-snapshot.c',
                                                        'syx-snapshot/syx-cow-cache.c',
                                                        'syx-snapshot/channel-buffer-writeback.c',
                                                    )])

libafl_ss.add(when : 'CONFIG_USER_ONLY', if_true : [files(
                                                          'user.c',
                                                          'hooks/syscall.c',
                                                    )])

