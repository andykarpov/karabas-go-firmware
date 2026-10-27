/*---------------------------------------------------------------------------/
/  Configurations of FatFs Module (karabas: the SD card of the firmware)
/  Based on ffconf.h of R0.15; see ff.h for the meaning of each option.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286	/* Revision ID */

#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	0
#define FF_USE_FIND		0
#define FF_USE_MKFS		0
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	3

/* the long names as SdFat gave them: UTF-8, the short names in code page 437 */
#define FF_CODE_PAGE	437
#define FF_USE_LFN		1
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	2
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12

/* no current dir: the firmware uses full paths, and f_readdir skips "." and ".." */
#define FF_FS_RPATH		0

#define FF_VOLUMES		1
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"SD"
#define FF_MULTI_PARTITION	0

#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		0
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

#define FF_FS_TINY		0
#define FF_FS_EXFAT		1
/* file times from the RTC: get_fattime() in src/storage */
#define FF_FS_NORTC		0
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
#define FF_FS_NOFSINFO	0

/* only APP uses the file system (the USB MSC mode goes past it) */
#define FF_FS_LOCK		0
#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000

/*--- End of configuration options ---*/
