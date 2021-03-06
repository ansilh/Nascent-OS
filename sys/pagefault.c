#include<sys/types.h>
#include <sys/page.h>
#include<sys/kprintf.h>
#include<sys/process.h>
#include <sys/pagefault.h>
#include<sys/page.h>
#include <sys/idt.h>
#include <sys/memset.h>
#include<sys/virmem.h>
#define MIN(a, b)  (a<b)? a : b
#define MAX(a, b)  (a>b)? a : b

void
page_fault_handler(uint64_t error_code)
{
    uint64_t faulting_addr = read_cr2();
    struct vma *vma_v = check_vma(faulting_addr);
    // kprintf("Faulting address cr2 - %p\n", faulting_addr);
    if (vma_v != NULL)
    {
        // FIXME: if page not present but u get page fault
        uint64_t vma_start = vma_v->start_addr;
        uint64_t pml_addr = (uint64_t)RunningTask->regs.cr3 + KERNBASE;
        // cow case
        if (error_code == 7)
        {
            // FIXME: do i have to make any preliminary checks?
            uint64_t page_phyaddr = 0;
            int present = get_pte_entry(pml_addr, faulting_addr, &page_phyaddr);
            struct page* page_addr = get_page_from_PA(page_phyaddr);
            if (present && (page_addr->ref_count == 1)) {
                UNSET_COW(page_phyaddr);
                SET_WRITE(page_phyaddr);
                set_mapping(pml_addr, faulting_addr, page_phyaddr, 1);
                invalidate_tlb(pml_addr);
            }
            else if(present && (page_addr->ref_count > 1)) {
                void * buff_page = page_alloc();
                memcopy((void *) ScaleDown((uint64_t *)faulting_addr), buff_page, PAGE_SIZE);

                // FIXME: check permissions here
                set_mapping(pml_addr, faulting_addr, ((uint64_t)buff_page - KERNBASE), 7);
                invalidate_tlb(pml_addr);

                // FIXME: call free here instead of decrementing ref_count
                page_addr->ref_count--;
            }
        }
            //elf case lazy loading
        else if ((vma_v->vmtype == TEXT) || (vma_v->vmtype == DATA))
        {
            uint64_t bss_addr = vma_start + vma_v->p_filesz;
            if(faulting_addr<bss_addr)
            {
                uint64_t phys_addr = (uint64_t) kmalloc();
                uint64_t s_d_a =  (uint64_t)ScaleDown((uint64_t*)faulting_addr);

                uint64_t offset = s_d_a-vma_start;

                set_mapping((uint64_t)pml_addr, s_d_a, (uint64_t)phys_addr, 7);
                //for lower segment
                if (s_d_a<vma_start &&(s_d_a+(4*1024))>bss_addr )
                {
                    memcopy((void *)(vma_v->tarfs_base), (void *) vma_start, vma_v->p_filesz);
                }
                else if(s_d_a<=vma_start)
                {
                    memcopy((void *)(vma_v->tarfs_base), (void *) vma_start, MIN(vma_v->p_filesz,4*1024-vma_start));
                }
                    //for upper segment
                else if ((s_d_a+(4*1024))>bss_addr)
                {
                    memcopy((void *)(vma_v->tarfs_base+offset), (void *) s_d_a, (bss_addr-s_d_a));
                }
                else
                {
                    memcopy((void *)(vma_v->tarfs_base+offset), (void *) s_d_a, 4*1024);
                }
            }
                //bss case
            else
            {
                uint64_t phys_addr = (uint64_t) kmalloc();
                uint64_t s_d_a =  (uint64_t)ScaleDown((uint64_t*)faulting_addr);
                set_mapping((uint64_t)pml_addr, s_d_a, (uint64_t)phys_addr, 7);
            }
        }
            //auto growing stack
        else if (vma_v->vmtype == STACK)
        {
            uint64_t phys_addr = (uint64_t) kmalloc();
            uint64_t s_d_a =  (uint64_t)ScaleDown((uint64_t*)faulting_addr);
            set_mapping((uint64_t)pml_addr, s_d_a, (uint64_t)phys_addr, 7);
        }

        else
        {
            kprintf("Generic Page fault");
        }

    }
    else
    {
        kprintf("Segmentation Fault: \n");
        print_status(error_code);
        sys_exit(RunningTask->pid);
    }
}

void
print_status(uint64_t e_c)
{
    if (e_c & PF_B_2)
    {
        kprintf("user(r3) |");
    }
    else
    {
        kprintf("kernel(r0) |");
    }

    if (e_c & PF_B_1)
    {
        kprintf("write | ");
    }
    else
    {
        kprintf("read mode | ");
    }

    if (e_c & PF_B_0)
    {
        kprintf("page present");
    }
    else
    {
        kprintf("page not present\n");
    }

}

struct vma *
check_vma(uint64_t v_addr)
{
    struct vma *itr = RunningTask->task_mm->vma_head;
    while (itr != NULL)
    {
        uint64_t start_addr = itr->start_addr;
        uint64_t end_addr = itr->end_addr;
        if (v_addr >= start_addr && v_addr <= end_addr)
        {
            return itr;
        }
        itr = itr->next;
    }
    return NULL;
}

