#ifndef _LIBCHARDRIVER_H_
#define _LIBCHARDRIVER_H_

struct chardriver {
	int (*cdr_open)(dev_t minor, int access);
	int (*cdr_close)(dev_t minor);
	ssize_t (*cdr_read)(dev_t minor, u64 pos,
	  endpoint_t endpoint, char* buf, unsigned int count);
	ssize_t (*cdr_write)(dev_t minor, u64 pos,
	  endpoint_t endpoint, char* buf, unsigned int count);
	int (*cdr_ioctl)(dev_t minor, int request, endpoint_t endpoint, char* buf);
	int (*cdr_mmap)(dev_t minor, endpoint_t endpoint, char* addr, off_t offset, size_t length, char** retaddr);
	void (*cdr_intr)(unsigned mask);
	void (*cdr_alarm)(clock_t timestamp);
};

PUBLIC void chardriver_process(struct chardriver* cd, MESSAGE* msg);
PUBLIC int chardriver_task(struct chardriver* cd);
PUBLIC void chardriver_reply(MESSAGE* msg, int retval);
PUBLIC int chardriver_get_minor(MESSAGE* msg, dev_t* minor);

#endif
