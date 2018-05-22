#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include "log.h"

#define RECEIVE_TIMEOUT 3
#define DISTR_TMP_PATH "/tmp/distr.xz"
#define MAX_DISTR_SIZE 300*1024*1024

/* 1. Файл дистрибутива принимается частями не более 64Кб
 * 2. Длина последней части должна быть равна 0.
 * 3. Таймаут приема равен RECEIVE_TIMEOUT c.
 */

static void *handle;
static int fd = -1;
static clock_t last_ts;	// time stump
static int fsize;

int save_distr_part(void *h, int len, void *data)
{
	clock_t cur_ts = clock();
	if (fd == -1) {
		fd = open(DISTR_TMP_PATH, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (fd == -1) {
			emd_log(LOG_DEBUG, "Ошибка открытия файла %s: %s", DISTR_TMP_PATH, strerror(errno));
			return -1;
		}
		handle = h;
	} else if (handle == h) {
		if ((cur_ts - last_ts)/CLOCKS_PER_SEC < 3) {
			emd_log(LOG_DEBUG, "Таймаут приема файла обновления");
			goto err;
		}

	} else {
		emd_log(LOG_DEBUG, "Попытка перезаписи файла дистрибутива другим клиентом");
		return -1;
	}

	if (len) {
		if (len + fsize > MAX_DISTR_SIZE) {
			emd_log(LOG_DEBUG, "Превышение максимально допустимой длины файла дистрибутива");
			goto err;
		}
		if (write(fd, data, len) == -1) {
			emd_log(LOG_DEBUG, "Ошибка записи файла %s: %s", DISTR_TMP_PATH, strerror(errno));
			goto err;
		}
	} else {
		close(fd);
		fd = -1;
		fsize = 0;
		// запустить в отдельном потоке, чтобы сначала ответить на запрос
		system("reboot");
	}
	last_ts = cur_ts;
	return 0;

err:
	close(fd);
	fd = -1; 
	remove(DISTR_TMP_PATH);
	fsize = 0;
	return -1;
}
