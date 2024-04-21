/*
unscrambler 0.5.3: unscramble not standard IVs scrambled DVDs thru 
bruteforce, intended for Gamecube/WII Optical Disks and DVD.

Copyright (C) 2006  Victor Muñoz (xt5@ingenieria-inversa.cl)
Copyright (C) 2018-2024  Sarami

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#pragma warning(disable:4710 4711)
#pragma warning(push)
#pragma warning(disable:4820)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#pragma warning(pop)
#include "ecma-267.h"

#define MAX_SEEDS 4

unsigned char b_in[0x9500];
unsigned char b_out[0x8000];

typedef struct t_seed {
    int seed;
    unsigned char streamcipher[2048];
} t_seed;


t_seed _seeds[(MAX_SEEDS+1)*16];

#ifndef _WIN32
int _fseeki64(FILE* fp, long ofs, int origin)
{
    return fseeko(fp, ofs, origin);
}

off_t _ftelli64(FILE* fp)
{
    return ftello(fp);
}
#endif

/* swap the endianess of a 32bit integer */
unsigned int swap32(unsigned int p) {
    return p<<24|((p<<8)&0xFF0000)|((p>>8)&0xFF00)|(p>>24);
}

/* add a seed to the cache */
t_seed *add_seed(t_seed *seeds, unsigned short seed) {
    int i;
        
    printf("caching seed %04x\n", seed);

    if(seeds->seed==-2) return NULL;

    seeds->seed=seed;
      
    LFSR_init(seeds->seed);

    for(i=0; i<2048; i++) seeds->streamcipher[i]=LFSR_byte();
    
    return seeds;
}

/* test if the current seed is the one used for this sector, the check is done 
comparing the EDC generated, with the one at the bottom of sector */
int test_seed(int j, unsigned long type) {
    int i,k;
    unsigned int edcPos;
    unsigned char buf[2064];
      
    LFSR_init(j);

    if (type == 0 || type == 1) {
        memcpy(buf, b_in, 2064);
        edcPos = 2060;
    }
    else if (type == 2) {
        for (k = 0; k < 12; k++) {
            memcpy(buf + 172 * k, b_in + 192 * k, 172);
        }
        edcPos = 2280;
    }
    else if (type == 3) {
        for (k = 0; k < 12; k++) {
            memcpy(buf + 172 * k, b_in + 182 * k, 172);
        }
        edcPos = 2170;
    }
    for(i=12; i<2060; i++) buf[i]^=LFSR_byte();
    if(edc_calc(0x00000000, buf, 2060) == swap32(*( (unsigned int *) (&b_in[edcPos]) )) ) {
        return 0;
    }
    return -1;
}

/* unscramble a complete frame, based on the seed already cached */
int unscramble_frame(t_seed *seed, unsigned char *_bin, unsigned char *_bout, int blockSize, unsigned long type) {
    unsigned char *bin;
    unsigned char *bout;
    unsigned int edc;
    unsigned int *_4bin;
    unsigned int *_4cipher;
    unsigned char buf[2064];

    int i,j,k;
    
    for(j=0; j< blockSize; j++) {
      
        bout=&_bout[0x800*j];
        
        if (type == 0 || type == 1) {
            bin = &_bin[0x810*j];
            _4bin = (unsigned int*)&bin[12];
        }
        else if (type == 2) {
            bin = &_bin[0x900 * j];
            for (k = 0; k < 12; k++) {
                memcpy(buf + 172 * k, bin + 192 * k, 172);
            }
            _4bin = (unsigned int*)&buf[12];
        }
        else if (type == 3) {
            bin = &_bin[0x950 * j];
            for (k = 0; k < 12; k++) {
                memcpy(buf + 172 * k, bin + 182 * k, 172);
            }
            _4bin = (unsigned int*)&buf[12];
        }
        _4cipher = (unsigned int*)seed->streamcipher;

        for(i=0; i<512; i++) _4bin[i]^=_4cipher[i];

        if (type == 2 || type == 3) {
            bin = buf;
        }
        if (type == 0) {
            memcpy(bout, bin+6, 2048); // copy CPR_MAI bytes
        }
        else {
            memcpy(bout, bin + 12, 2048);
        }

        edc=edc_calc(0x00000000, bin, 2060);
        if(edc != swap32(*( (unsigned int *) (&bin[2060]) )) ) {
            printf("error: bad edc (%08x) must be %08x\n", edc, swap32(*( (unsigned int *) (&bin[2060]) )));
            return -1;
        }
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    int i,j,s;
    int ret;
    size_t readSize;
    size_t writeSize;
    unsigned long type;
    char* endptr;

    FILE *in, *out;
    unsigned long long rawFileSize;
    int totalSectorSize;
    int blockSize;
    int lastSectorsPerBlock;

    time_t start;
    
    t_seed *seeds;
    t_seed *current_seed;
    
    printf("GOD/WOD/DVD unscrambler 0.5.3 (xt5@ingenieria-inversa.cl)\n\n"
           "This program is distributed under GPL license, \n"
           "see the LICENSE file for more info.\n\n");
    if(argc<4) {
        printf(
        	"Usage\n"
            "unscrambler.exe input output type\n"
        	"\tinput: .raw file\n"
        	"\toutput: .iso file\n"
            "\ttype\t0: Nintendo disc, 1: DVD 2064 bytes/sector\n"
            "      \t2: DVD 2304 bytes/sector, 3: DVD 2384 bytes/sector\n"
            );
        return 0;
    }
    
    in=fopen(argv[1],"rb");
    if(!in) {
        fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }
    out=fopen(argv[2],"wb");
    if(!out) {
        fprintf(stderr, "can't open %s\n", argv[2]);
        return 2;
    }
    
    type = strtoul(argv[3], &endptr, 10);
    if (*endptr) {
        fprintf(stderr, "[%s] is invalid argument. Please input integer.\n", endptr);
        return -1;
    }
    else if (type > 2) {
        fprintf(stderr, "[%s] is invalid argument. Please input 0, 1 or 2.\n", endptr);
        return -1;
    }

    _fseeki64(in, 0, SEEK_END);
    rawFileSize = (unsigned long long)_ftelli64(in);
    rewind(in);

    for(i=0; i<16; i++) {
        for(j=0; j<MAX_SEEDS; j++) {
            _seeds[i*MAX_SEEDS+j].seed=-1;
        }
        _seeds[i*MAX_SEEDS+j].seed=-2;
    }
    
    ret=0;
    
    s=0;
    start=time(0);

    blockSize = 16;
    if (type == 2) {
        readSize = (size_t)(0x900 * blockSize);
        totalSectorSize = (int)(rawFileSize / 0x900);
    }
    else if (type == 3) {
        readSize = (size_t)(0x950 * blockSize);
        totalSectorSize = (int)(rawFileSize / 0x950);
    }
    else {
        readSize = (size_t)(0x810 * blockSize);
        totalSectorSize = (int)(rawFileSize / 0x810);
    }
    writeSize = (size_t)(0x800 * blockSize);
    lastSectorsPerBlock = totalSectorSize % blockSize;

    fread(b_in, 1, readSize, in);
    while (!feof(in) && !ferror(in)) {
        seeds=&_seeds[((s>>4)&0xF)*MAX_SEEDS];
        while((seeds->seed)>=0) {
            if(!test_seed(seeds->seed, type)) {
                current_seed=seeds;
                goto seed_found;
            }
            seeds++;
        }
        
        for(j=0; j<0x7FFF; j++) {
            if(!test_seed(j, type)) {
                current_seed=add_seed(seeds,j);
                //printf("caching at %x\n", s);
                if(current_seed==NULL) {
                    fprintf(stderr, "no enough cache space for this seed.\n");
                    ret=3;
                    goto finish;
                }
                goto seed_found;
            }
        }
        fprintf(stderr, "no seed found for recording frame %d.\n", s>>4);
//        ret=4;
        ret=s>>4;
        goto finish;
        
        seed_found:
        
        if(unscramble_frame(current_seed, b_in, b_out, blockSize, type)) {
            fprintf(stderr, "error unscrambling recording frame %d.\n", s>>4);
//            ret=5;
            ret=s>>4;
            goto finish;
        }
        
        if(fwrite(b_out, 1, writeSize, out)!= writeSize) {
            fprintf(stderr, "can't write to the output file, check if there is enough free space.\n");
            ret=6;
            goto finish;        
        }
        
        s+= blockSize;
        if (s > totalSectorSize - blockSize && lastSectorsPerBlock > 0) {
            s += lastSectorsPerBlock;
            blockSize = lastSectorsPerBlock;
            if (type == 2) {
                readSize = (size_t)(0x900 * lastSectorsPerBlock);
            }
            else if (type == 3) {
                readSize = (size_t)(0x950 * lastSectorsPerBlock);
            }
            else {
                readSize = (size_t)(0x810 * lastSectorsPerBlock);
            }
            writeSize = (size_t)(0x800 * lastSectorsPerBlock);
        }
        fread(b_in, 1, readSize, in);
    }
    
    printf("image successfully unscrambled.\n");
    
    finish:
    
    printf("time elapsed: %.2lf seconds.\n", difftime(time(0), start));

    fclose(in);
    fclose(out);

    return ret;
}



