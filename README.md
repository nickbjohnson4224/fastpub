FastPub
=======

Zero-copy non-blocking shared memory publisher/subscriber IPC library

This is the result of me needing a shared memory publisher-subscriber protocol that would not have any copying overhead at all. The protocol is conceptually pretty simple, and I hope to improve the interface a lot in the future.

The protocol's core algorithm is essentially an extension of triple buffering, with the addition of multiple readers. The writer writes to one buffer, while a "current" buffer is always available for readers to grab. When all readers release a buffer that is not current, it falls into a "slack" pool, where the writer can grab it when it needs to produce a new buffer. As long as there are n + 2 total buffers, where n is the number of readers, the writer will always have at least one slack buffer available and the readers will always have the current buffer to grab, so neither side will ever block. For the trivial case of one reader, this is exactly triple buffering.

The buffers are all preallocated and stay in the shared memory region, and are also never copied, which means that this algorithm should have pretty close to optimal performance for just moving buffers of data between processes. The strict no-blocking guarantee makes it suitable for soft realtime applications, like realtime computer vision and robot control (which is the original intended purpose). Of course, it will readily (but safely) drop buffers that are not picked up by subscribers, and will only provide the most recent update at any one time, so it is not a general-purpose IPC mechanism by any means.
