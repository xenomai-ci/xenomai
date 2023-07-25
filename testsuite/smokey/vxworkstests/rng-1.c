// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskLib.h>
#include <vxworks/rngLib.h>

static struct traceobj trobj;

#define ADD_CONTENT(buffer,bytes,counter)				\
	{								\
		char	*bufPtr = buffer;				\
		for (k=0; k<bytes; k++) {				\
			*bufPtr = counter;				\
			counter ++;					\
			bufPtr ++;					\
		}							\
	}

#define CHECK_CONTENT(buffer,bytes,counter)				\
	{								\
		char	*bufPtr = buffer;			\
		for (k=0; k<bytes; k++) {				\
			traceobj_assert(&trobj, *bufPtr == (char)counter);	\
			counter ++;					\
			bufPtr ++;					\
		}							\
	}

static void rootTask(long arg, ...)
{
	int j, res, k, chunks;
	const int putBytes = 10;
	const int nrChunks = 3;
	const int rngBytes = putBytes * nrChunks;
	char buffer[putBytes];
	char bigBuffer[putBytes * 2 * nrChunks];
	int bytesPut;
	int bytesGot;
	int expectedCounter = 0;
	int checkCounter = 0;
	RING_ID rng;
	char not_a_ring[32];

	traceobj_enter(&trobj);
	ADD_CONTENT(buffer, sizeof(buffer), expectedCounter);
	rng = rngCreate(1);
	traceobj_assert(&trobj, rngIsEmpty(rng));
	memset(buffer, 0, sizeof(buffer));
	buffer[0] = 17;
	rngBufPut(rng, buffer, 1);
	traceobj_assert(&trobj, rngIsFull(rng));
	rngBufGet(rng, buffer, 1);
	traceobj_assert(&trobj, rngIsEmpty(rng));
	buffer[0] = 34;
	rngBufPut(rng, buffer, 1);
	traceobj_assert(&trobj, rngIsFull(rng));
	expectedCounter = 0;
	rngDelete(rng);

	/* Here real vxWorks 6.6 just return ERROR */
	memset(not_a_ring, 0, sizeof(not_a_ring));
	errnoSet(0);
	res = rngBufPut((RING_ID) & not_a_ring[0], buffer, 1);
	traceobj_assert(&trobj, res == ERROR);

/*	rng = rngCreate(1 * 1024 * 1024 * 1024);
	traceobj_assert(&trobj, res == ERROR);
	traceobj_assert(&trobj, errnoGet() == S_memLib_NOT_ENOUGH_MEMORY);
 */
	rng = rngCreate(rngBytes);
	traceobj_assert(&trobj, rng != 0);
	traceobj_assert(&trobj, rngIsEmpty(rng));
	traceobj_assert(&trobj, !rngIsFull(rng));

	/* Fill a few chunks */
	for (chunks = 0; chunks < nrChunks; chunks++) {
		traceobj_assert(&trobj,
				rngNBytes(rng) == chunks * (int)sizeof(buffer));
		traceobj_assert(&trobj,
				rngFreeBytes(rng) ==
				rngBytes - chunks * (int)sizeof(buffer));
		for (j = 0; j < (int)sizeof(buffer); j++) {
			buffer[j] = (char)j + (int)sizeof(buffer) * chunks;
		}
		ADD_CONTENT(buffer, sizeof(buffer), checkCounter);
		bytesPut = rngBufPut(rng, &buffer[0], sizeof(buffer));
		traceobj_assert(&trobj, bytesPut == sizeof(buffer));
		traceobj_assert(&trobj, !rngIsEmpty(rng));
		traceobj_assert(&trobj,
				rngIsFull(rng) == (nrChunks - 1 == chunks));
		traceobj_assert(&trobj,
				rngFreeBytes(rng) ==
				rngBytes - bytesPut * (chunks + 1));
		traceobj_assert(&trobj,
				rngNBytes(rng) ==
				(chunks + 1) * (int)sizeof(buffer));
	}
	traceobj_assert(&trobj, rngIsFull(rng));
	ADD_CONTENT(buffer, sizeof(buffer), checkCounter);
	bytesPut = rngBufPut(rng, &buffer[0], sizeof(buffer));
	traceobj_assert(&trobj, bytesPut == 0);
	traceobj_assert(&trobj, rngIsFull(rng));

	/* Read chunks back and check content */
	for (chunks = 0; chunks < nrChunks; chunks++) {
		memset(buffer, 0, sizeof(buffer));
		traceobj_assert(&trobj,
				rngNBytes(rng) ==
				(nrChunks - chunks) * (int)sizeof(buffer));
		traceobj_assert(&trobj,
				rngFreeBytes(rng) ==
				chunks * (int)sizeof(buffer));
		bytesGot = rngBufGet(rng, &buffer[0], sizeof(buffer));
		traceobj_assert(&trobj, bytesGot == (int)sizeof(buffer));
		CHECK_CONTENT(buffer, bytesGot, expectedCounter);
		traceobj_assert(&trobj, !rngIsFull(rng));
		traceobj_assert(&trobj,
				rngIsEmpty(rng) == (chunks == nrChunks - 1));

		traceobj_assert(&trobj,
				rngFreeBytes(rng) ==
				(chunks + 1) * (int)sizeof(buffer));
		traceobj_assert(&trobj,
				rngNBytes(rng) ==
				(nrChunks - chunks - 1) * (int)sizeof(buffer));
	}

	/* Testing filling too many */
	ADD_CONTENT(bigBuffer, sizeof(bigBuffer), checkCounter)
	bytesPut = rngBufPut(rng, &bigBuffer[0], sizeof(bigBuffer));
	traceobj_assert(&trobj, bytesPut == rngBytes);
	traceobj_assert(&trobj, !rngIsEmpty(rng));
	traceobj_assert(&trobj, rngIsFull(rng));
	traceobj_assert(&trobj, rngFreeBytes(rng) == 0);
	traceobj_assert(&trobj, rngNBytes(rng) == rngBytes);

	/* Getting too many */
	memset(bigBuffer, 0, sizeof(bigBuffer));
	bytesGot = rngBufGet(rng, &bigBuffer[0], sizeof(bigBuffer));
	traceobj_assert(&trobj, bytesGot == rngBytes);
	traceobj_assert(&trobj, rngIsEmpty(rng));
	traceobj_assert(&trobj, !rngIsFull(rng));
	traceobj_assert(&trobj, rngFreeBytes(rng) == rngBytes);
	traceobj_assert(&trobj, rngNBytes(rng) == 0);

	/* Now we need to adjust our expectedCounter */
	expectedCounter += sizeof(buffer);

	CHECK_CONTENT(bigBuffer, bytesGot, expectedCounter);

	ADD_CONTENT(bigBuffer, sizeof(bigBuffer), checkCounter);
	bytesPut = rngBufPut(rng, &bigBuffer[0], sizeof(bigBuffer));
	traceobj_assert(&trobj, bytesPut == rngBytes);
	rngFlush(rng);
	traceobj_assert(&trobj, rngIsEmpty(rng));
	traceobj_assert(&trobj, !rngIsFull(rng));
	traceobj_assert(&trobj, rngFreeBytes(rng) == rngBytes);
	traceobj_assert(&trobj, rngNBytes(rng) == 0);
	while (bytesGot > 0) {
		bytesGot = rngBufGet(rng, &bigBuffer[0], sizeof(bigBuffer));
		CHECK_CONTENT(bigBuffer, bytesGot, expectedCounter);
	}
	rngDelete(rng);

	chunks = 10;
	rng = rngCreate(chunks);
	bytesPut = 5;
	traceobj_assert(&trobj, rngFreeBytes(rng) > bytesPut);
	checkCounter = 0xaa;
	expectedCounter = checkCounter;
	for (j = 0; j < bytesPut; j++) {
		rngPutAhead(rng, checkCounter, j);
		checkCounter++;
	}
	rngMoveAhead(rng, bytesPut);
	bytesGot = rngBufGet(rng, &bigBuffer[0], sizeof(bigBuffer));
	traceobj_assert(&trobj, bytesGot == bytesPut);
	CHECK_CONTENT(bigBuffer, bytesGot, expectedCounter);

	/* Check also wrap-around */
	bytesPut = chunks -2;
	traceobj_assert(&trobj, rngFreeBytes(rng) > bytesPut);
	checkCounter = 0xaa;
	expectedCounter = checkCounter;
	for (j = 0; j < bytesPut; j++) {
		rngPutAhead(rng, checkCounter, j);
		checkCounter++;
	}
	rngMoveAhead(rng, bytesPut);
	bytesGot = rngBufGet(rng, &bigBuffer[0], sizeof(bigBuffer));
	traceobj_assert(&trobj, bytesGot == bytesPut);
	CHECK_CONTENT(bigBuffer, bytesGot, expectedCounter);
	rngDelete(rng);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;

	traceobj_init(&trobj, argv[0], 0);

	tid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_join(&trobj);

	exit(0);
}
