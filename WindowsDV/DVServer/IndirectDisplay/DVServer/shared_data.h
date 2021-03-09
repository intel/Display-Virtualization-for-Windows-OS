#ifndef _SHARED_DATA_H
#define _SHARED_DATA_H

#pragma pack(push)
#pragma pack(1)

#define MAX_DATA_SIZE 256

typedef struct _edid_data {
	int size;
	char data[MAX_DATA_SIZE];
} edid_data;

#pragma pack(pop)

#endif /* _SHARED_DATA_H */
