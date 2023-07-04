/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */
// #define __LIBRARY__ Ϊ�˰��������� unistad.h �е���Ƕ���������Ϣ��
#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */

 // ʵ����ֻ�� pause �� fork ��Ҫʹ��������ʽ���Ա�֤�� main() �в���Ū��
 // ��ջ��������ͬʱ����������һЩ������

 // _syscall0() �� unistd.h �е���Ƕ����롣
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

#include <string.h>

static char printbuf[1024];				//��̬�ַ������飬�����ں���ʾ��Ϣ�Ļ��档

extern char *strcpy();
extern int vsprintf();
extern void init(void);				//����ԭ�ͣ���ʼ��
extern void blk_dev_init(void);			//���豸��ʼ���� blk_drv/ll_re_blk.c
extern void chr_dev_init(void);			//�ַ��豸��ʼ�� chr_drv/tty_io.c
extern void hd_init(void);					//Ӳ�̳�ʼ��	blk_drv/hd.c
extern void floppy_init(void);			//������ʼ�� blk_drv/floppy.c
extern void mem_init(long start, long end);			//�ڴ�����ʼ�� mm/memory.c
extern long rd_init(long mem_start, int length);	//�����̳�ʼ�� blk_drv/ramdisk.c
extern long kernel_mktime(struct tm * tm);		//����ϵͳ��������ʱ��(��)

// �ں�ר�� sprintf() ������������ʽ����Ϣ�������ָ�������� str �С�
static int sprintf(char * str, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(str, fmt, args);
	va_end(args);
	return i;
}

/*
 * This is set up by the setup-routine at boot-time
 */
 // ��Щ�������ں������ڼ�� setup.s �������á�
#define EXT_MEM_K (*(unsigned short *)0x90002)	//1MB �Ժ����չ�ڴ��С(KB)
#define CON_ROWS ((*(unsigned short *)0x9000e) & 0xff)	//ѡ���Ŀ���̨��Ļ������
#define CON_COLS (((*(unsigned short *)0x9000e) & 0xff00) >> 8)
#define DRIVE_INFO (*(struct drive_info *)0x90080)	//Ӳ�̲�����32�ֽ�����
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)	//���ļ�ϵͳ�����豸��
#define ORIG_SWAP_DEV (*(unsigned short *)0x901FA)	//�����ļ������豸��

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
// ��κ��ȡ CMOS ʵʱʱ�����ݡ�outb_p �� inb_p �� include/asm/io.h �ж����
// �˿���������ꡣ
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

//�� BCD ��ת���ɶ�������ֵ��
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

// CMOS �ķ���ʱ�������Ϊ�˼�Сʱ�����ڶ�ȡ������ѭ����������ֵ������ʱ CMOS
// ����ֵ�����˱仯�������¶�ȡ�������ܿ��������1s�ڡ�
static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;		//ti_mon �е��·ݷ�Χ�� 0 ~ 11��
	startup_time = kernel_mktime(&time);	//���㿪��ʱ�䡣
}

static long memory_end = 0;			//���������е������ڴ�����
static long buffer_memory_end = 0;		//���ٻ�����ĩ�˵�ַ
static long main_memory_start = 0;		//���ڴ濪ʼ��λ��
static char term[32];			//�ն������ַ���

// ��ȡ��ִ�� /etc/rc �ļ�ʱ��ʹ�õ������в����ͻ���������
static char * argv_rc[] = { "/bin/sh", NULL };		//����ִ�г���ʱ�������ַ�����ֵ��
static char * envp_rc[] = { "HOME=/", NULL ,NULL };//����ִ�г���ʱ�Ļ����ַ�����ֵ��

//���е�¼shellʱ��ʹ�õ������кͻ���������
//argv[0]�е��ַ���-���Ǵ���shell����sh��һ����ʾλ��ͨ�������ʾλ��sh�������Ϊ
//shell����ִ�С�
static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL, NULL };

//���ڴ��Ӳ�̲�����
struct drive_info { char dummy[32]; } drive_info;

//�ں˳�ʼ��������
// ������ void����Ϊ�� head.s ������ô�����(�� main �ĵ�ַѹ���ջ��ʱ��)��
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
/*
 * ��ʱ�жϻ�����ֹ�ģ������Ҫ�����ú�ͽ��俪����û���걸���ж���Ӧ
 * �ֻ���˵���ɻ�ȱ���ж���Ӧ��ϵͳһ�������޷�������Ӧ�жϣ�����JJ�ˣ�
 * �ֻ��������ǲ�ȥ������Ȼ���ǵ�׼�������ٿ����ȽϺ���
 */
 	ROOT_DEV = ORIG_ROOT_DEV;		// ROOT_DEV ������ fs/super.c �С�
 	SWAP_DEV = ORIG_SWAP_DEV;		// SWAP_DEV ������ mm/swap.c �С�
	sprintf(term, "TERM=con%dx%d", CON_COLS, CON_ROWS);
	envp[1] = term;
	envp_rc[1] = term;
 	drive_info = DRIVE_INFO;

	// ���Ÿ��ݻ��������ڴ��������ø��ٻ����������ڴ�����λ�úͷ�Χ��
	// ���ٻ���ĩ�˵�ַ > buffer_memory_end
	// �����ڴ����� > memory_end
	// ���ڴ濪ʼ��ַ > main_memory_start
	memory_end = (1<<20) + (EXT_MEM_K<<10); //1M + ��չ�ڴ��С
	memory_end &= 0xfffff000;			//���Բ���4K(1ҳ)���ڴ���
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024)
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;	//���ڴ濪ʼ��ַ = ���ٻ�����������ַ
#ifdef RAMDISK	//��������������̣������ڴ滹����Ӧ����
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif

// �������ں˽������з���ĳ�ʼ�����̡�
	mem_init(main_memory_start,memory_end);		//���ڴ�����ʼ��
	trap_init();		//�����ų�ʼ��
	blk_dev_init();	//���豸��ʼ��
	chr_dev_init();	//�ַ��豸��ʼ��
	tty_init();		//tty��ʼ��
	time_init();	//���ÿ�������ʱ��
	sched_init();	//���ȳ����ʼ��
	buffer_init(buffer_memory_end);	//��������ʼ�������ڴ������
	hd_init();	//Ӳ�̳�ʼ��
	floppy_init();	//������ʼ��
	sti();		//�����ж�
//�������ͨ���ڶ�ջ�����õĲ����������жϷ���ָ���������� 0 ִ�С�
	move_to_user_mode();
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
/*
 * ��Ҫ!! �����κ����������񣬡�pause()������ζ�����Ǳ���ȴ��յ�yoga�źŲŻ᷵��
 * ����̬�������� 0 ��Ψһ��������(�μ���schedule()��)����Ϊ���� 0 ���κο���ʱ
 * ���ﶼ�ᱻ����,��˶������� 0 'pause()'����ζ�����Ƿ������鿴�Ƿ������������
 * �����У����û�еĻ������Ǿ�������һֱѭ��ִ�С�pause()����
 */

 // pause()ϵͳ���û������ 0 ת���ɿ��жϵȴ�״̬����ִ�е��Ⱥ��������ǵ��Ⱥ���
 // ����ϵͳ��û����������������оͻ��л������� 0�������������� 0 ��״̬��
	for(;;)
		__asm__("int $0x80"::"a" (__NR_pause):"ax");//ִ��ϵͳ����pause()
}

//������ʽ����Ϣ���������׼����豸stdout(1)��������ָ��Ļ����ʾ��
//write()�� ��һ������ 1 -> stdout����ʾ���������������������׼�豸��
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// init()�������������� 0 ��һ�δ������ӳ��� ���� 1���С����ȶԵ�һ����Ҫִ�еĳ�
//��(shell)�Ļ������г�ʼ����Ȼ���Ե�¼shell��ʽ���ظó���ִ�С�
void init(void)
{
	int pid,i;

// setup() ��һ��ϵͳ���á�������ȡӲ�̲���������������Ϣ������������(������ڵĻ�)
//�Ͱ�װ���ļ�ϵͳ�豸��
	setup((void *) &drive_info);

// �����Զ�д��ʽ�򿪡�/dev/tty1��,����Ӧ�ն˿���̨����Ϊ���ǵ�һ�δ��ļ�������
//���Բ������ļ������(�ļ�������)�϶��� 0��
	(void) open("/dev/tty1",O_RDWR,0);
	(void) dup(0);		//���ƾ�����������1�� -- stdout ��׼����豸
	(void) dup(0);		//���ƾ�����������2�� -- stderr ��׼��������豸

//�����ӡ���������������ֽ�����ÿ��1024�ֽڣ��Ѿ����ڴ��������ڴ��ֽ�����
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

//����ͨ��fork()���ڴ���һ���ӽ���(���� 2)�����ڱ��������ӽ��̣�fork()������ 0 ֵ������
//ԭ�����򷵻��ӽ��̵Ľ��̺� pid��
//�����ӽ���(���� 2)�رվ�� 0 (stdin)����ֻ����ʽ�� /etc/rc �ļ�����ʹ��execve()��
//�������������滻�� /bin/sh ����Ȼ��ִ�� /bin/sh ������Я���Ĳ����ͻ��������ֱ���
//argv_rc �� envp_rc ����������رվ�� 0 ������ /etc/rc �ļ� ��Ϊ�˰� stdin �ض���
//�� /etc/rc �ļ������� shell ���� /bin/sh �Ϳ������� /etc/rc �����õ������ˡ���������
//�� sh �����з�ʽ�Ƿǽ���ʽ�ģ�����ִ����rc�ļ��е�����������˳���
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}

//�����Ǹ�����(1)ִ�е���䡣wait()�ȴ��ӽ���ֹͣ����ֹ������ֵ����Ӧ���ӽ��̵Ľ��̺š�
//�����Ǹ������ڵȴ��ӽ���(���� 2)�˳���&i �Ǵ�ŷ���״̬��Ϣ��λ�á�
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;

//���ִ�е�������˵���ղŴ������ӽ����Ѿ������ˡ�������ѭ���������ٴ���һ���ӽ��̡�
//�������˳�����ӡ��Ϣ���ظ�ѭ����ȥ��һֱ���������ѭ���
	while (1) {
//�����������ʾ����ʼ�������ӳ���ʧ�ܡ���Ϣ������ִ�С�
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
//�µ��ӽ��̣��رվ��(0,1,2)���´���һ���Ự�����ý�����ţ�Ȼ�����´� /dev/tty0 ��Ϊ
// stdin�������Ƴ� stdout �� stderr���ٴ�ִ��/bin/sh�����ʹ����һ�ײ����ͻ��������顣
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty1",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		// Ȼ�󸸽����ٴ����� wait() �ȴ���
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();				//ͬ��������ˢ�»�����
	}
	_exit(0);	/* NOTE! _exit, not exit() */
	//_exit �� exit ��������������ֹһ����������_exit()ֱ����һ�� sys_exit��ϵͳ���ã���
	//exit() ������ͨ�������е�һ�������������Ƚ���һЩ����������������ִ�и���ֹ�������
	//�ر����б�׼IO�ȣ�Ȼ�����sys_exit��
}
