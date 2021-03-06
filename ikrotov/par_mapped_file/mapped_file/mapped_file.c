#include "../chunk_manager/chunk_manager.h"
#include "mapped_file.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include "semaphore.h"


static int mf_get_index_and_length(off_t offset, size_t size, off_t* index, off_t* len, int pg_size) {
    off_t pa_offset = offset & ~(pg_size - 1);
    off_t new_size = size + offset - pa_offset;
    *index = pa_offset / pg_size;
    *len = new_size / pg_size;
    if(new_size % pg_size) (*len)++;
    
    return 0;
}

mf_handle_t mf_open(const char *pathname) {
    if(pathname == NULL) {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(pathname, O_RDWR, 0666);
    if(fd == -1) {
        return NULL;
    }
    
    chunk_pool_t* cpool = chunk_pool_init(fd, PROT_READ | PROT_WRITE);
    if(!cpool) {
        return NULL;
    }
    return (mf_handle_t)cpool;
}

int mf_close(mf_handle_t mf) {
    if(mf == NULL) {
        return -1;
    }
    sem_wait(&((chunk_pool_t*)mf)->semaphore);
    int err = chunk_pool_deinit((chunk_pool_t*)mf);
    if(err) {
        sem_post(&((chunk_pool_t*)mf)->semaphore);
        return err;
    }
    sem_post(&((chunk_pool_t*)mf)->semaphore);
    return 0;
}

off_t mf_file_size(mf_handle_t mf) {
    if(mf == MF_OPEN_FAILED) {
        return -1;
    }
    
    int fd = ((chunk_pool_t*)mf)->fd;
    if(fd < 0) return -1;
    
    struct stat sb = {0};
    int err = fstat(fd, &sb);
    if(err) return -1;
    
    return sb.st_size;
}

void *mf_map(mf_handle_t mf, off_t offset, size_t size, mf_mapmem_handle_t *mapmem_handle) {
    if( mf == MF_OPEN_FAILED || mapmem_handle == NULL || offset < 0) {
        errno = EINVAL;
        return NULL;
    }
    
    chunk_pool_t *cpool = (chunk_pool_t *)mf;
    chunk_t **chunk_ptr = (chunk_t **)mapmem_handle;
    int pagesize = cpool->pg_size;
    off_t index, length;
	size = min(size, pagesize);
    mf_get_index_and_length(offset, size, &index, &length, pagesize);

    sem_wait(&cpool->semaphore);
    if(cpool->is_mapped == 0) {
        off_t file_size, index_file;
        mf_get_index_and_length(offset, cpool->file_size, &index_file, &file_size, pagesize); 
        *chunk_ptr = chunk_init(0, file_size, cpool);
        cpool->last_used = *chunk_ptr;
        cpool->is_mapped = 1;
    }

    int err = chunk_pool_find(cpool, chunk_ptr, index, length);
    if(err == ENOKEY) {
        *chunk_ptr = chunk_init(index, length+length/2, cpool);
        if(!*chunk_ptr) {
            *mapmem_handle = MF_MAP_FAILED;
            sem_post(&cpool->semaphore);
            return NULL;
        }
        sem_post(&cpool->semaphore);
        return (void*)((off_t)((*chunk_ptr)->data) + (offset - index*get_chunk_size(1)));
    } else if(err) {
        sem_post(&cpool->semaphore);
        return NULL;
    } else {
        sem_post(&cpool->semaphore);
        return (void*)((off_t)((*chunk_ptr)->data) +
                       (offset - (off_t)((*chunk_ptr)->index)*get_chunk_size(1)));
    }
}

int mf_unmap(mf_handle_t mf, mf_mapmem_handle_t mapmem_handle) {
    if( mf == MF_OPEN_FAILED || mapmem_handle == NULL) {
        errno = EINVAL;
        return NULL;
    }
    
    chunk_pool_t *cpool = (chunk_pool_t *)mf;
    chunk_t *chunk_ptr = (chunk_t *)mapmem_handle;
    sem_wait(&cpool->semaphore);
    if((chunk_ptr)->ref_counter > 1) {
        (chunk_ptr)->ref_counter -= 1;
        sem_post(&cpool->semaphore);
        return 0;
    } else if ((chunk_ptr)->ref_counter == 1){
        int err = chunk_release(chunk_ptr);
        if(err) {
            sem_post(&cpool->semaphore);
            return err;
        }
        sem_post(&cpool->semaphore);
        return 0;
    } else {
        sem_post(&cpool->semaphore);
        return 0;
    }
}

ssize_t mf_read(mf_handle_t mf, void* buf, size_t count, off_t offset) {
    off_t file_size = ((chunk_pool_t*)mf)->file_size;
    if(file_size == -1) {
        errno = EBADF;
        return -1;
    }
    
    if( mf == MF_OPEN_FAILED || buf == NULL || offset < 0 || offset > file_size ) {
        errno = EINVAL;
        return -1;
    }
    
    chunk_pool_t *cpool = (chunk_pool_t *)mf;
    count = min(count, file_size - offset);
    int pagesize = cpool->pg_size;

    off_t index, len;
    
    mf_get_index_and_length(offset, count, &index, &len, pagesize);

    chunk_t* read_chunk;
    sem_wait(&cpool->semaphore);
    if(cpool->is_mapped == 0) {
        off_t file_size, index_file;
        mf_get_index_and_length(offset, cpool->file_size, &index_file, &file_size, pagesize); 
        read_chunk = chunk_init(0, file_size, cpool);
        cpool->last_used = read_chunk;
        cpool->is_mapped = 1;
    }

  
    int err = chunk_pool_find(cpool, &read_chunk, index, len);
    if(err == ENOKEY) {
        read_chunk = chunk_init(index, len+len/2, cpool);
        if(!read_chunk) {
            sem_post(&cpool->semaphore);
            return -1;
        }
    } else if (err) {
        sem_post(&cpool->semaphore);
        return -1;
    }
   
    
    memcpy(buf, (const void*)((off_t)(read_chunk->data) + (off_t)(offset - read_chunk->index*pagesize)), count);
    sem_post(&cpool->semaphore);
    return count;
}

ssize_t mf_write(mf_handle_t mf, const void* buf, size_t count, off_t offset) {
    off_t file_size = mf_file_size(mf);
    if(file_size == -1)
    {
        return -1;
    }
    
    if( mf == MF_OPEN_FAILED || buf == NULL || offset < 0 || offset > file_size ) {
        errno = EINVAL;
        return -1;
    }
    
    chunk_pool_t *cpool = (chunk_pool_t *)mf;
    int pagesize = cpool->pg_size;
    
    count = min(count, file_size - offset);
    
    off_t index, len;
    mf_get_index_and_length(offset, count, &index, &len, pagesize);
    sem_wait(&cpool->semaphore);
    chunk_t* write_chunk;
   if(cpool->is_mapped == 1) {
        write_chunk = cpool->last_used;
        write_chunk->ref_counter += 1;
    } else {
        int err = chunk_pool_find(cpool, index, len, &write_chunk);
        if(err == ENOKEY) {
            write_chunk = chunk_init(index, len, cpool);
            if(!write_chunk) {
                sem_post(&cpool->semaphore);
                return -1;
            }
        } else if (err) {
            sem_post(&cpool->semaphore);
            return -1;
        }
    }
    
    void* dst = (void*)((off_t)(write_chunk->data) + (offset - write_chunk->index*pagesize));
    memcpy(dst, (const void*)buf, count);
    sem_post(&cpool->semaphore);
    return count;
}

