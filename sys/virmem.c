#include <sys/page.h>
#include <sys/types.h>
#include <sys/virmem.h>
#include <sys/kprintf.h>
#include <sys/memset.h>
uint64_t *kpml_addr;

void init_mem(uint64_t *physfree, uint32_t *modulep, uint64_t *mem_end)
{
    // Initiating important vars
    pages = (struct page *) physfree;
    free_page_head = NULL;

    create_page_list(physfree, modulep, mem_end);
    struct page *page1 = page_alloc();

    kpml_addr = (uint64_t *) page1;
    create_vir_phy_mapping(kpml_addr);

    //test_mapping(kpml_addr);

    LOAD_CR3((uint64_t *) get_phyaddr((uint64_t) kpml_addr));
}

void test_mapping(uint64_t *pml_addr)
{
    get_mapping((uint64_t) pml_addr, KERNBASE);
    get_mapping((uint64_t) pml_addr, (KERNBASE + PHYSBASE));
    get_mapping((uint64_t) pml_addr, KERNBASE + 0xb8000);
}

void create_vir_phy_mapping(uint64_t *pml_addr)
{
    int cnt = 0;
    while (cnt < page_count)
    {
        uint64_t viraddr = KERNBASE + cnt * PAGE_SIZE;
        uint64_t phyaddr = (0x00 + cnt * PAGE_SIZE);
        // FIXME: to give user permissions or not? or override it later
        set_mapping((uint64_t)pml_addr,(uint64_t) viraddr, phyaddr, 7);
        cnt++;
    }
}

int get_pte_entry(uint64_t pml_addr, uint64_t viraddr, uint64_t *phy_addr) {
    #define SCLDN(vaddr) (((vaddr >> 12)) << 12)

    int pml4_id = (viraddr >> 39) & 0x1FF;
    int pdpte_id = (viraddr >> 30) & 0x1FF;
    int pde_id = (viraddr >> 21) & 0x1FF;
    int pte_id = (viraddr >> 12) & 0x1FF;

    uint64_t* tmp = (uint64_t*)SCLDN(pml_addr);
    if (!(*(tmp + pml4_id) & 1)) {
        return 0;
    }
    uint64_t pdpte = tmp[pml4_id] + KERNBASE;
    tmp = (uint64_t*)SCLDN(pdpte);
    if (!(*(tmp + pdpte_id) & 1)) {
        return 0;
    }
    uint64_t pde = tmp[pdpte_id] + KERNBASE;
    tmp = (uint64_t*)SCLDN(pde);
    if (!(*(tmp + pde_id) & 1)) {
        return 0;
    }
    uint64_t pte = tmp[pde_id] + KERNBASE;
    tmp = (uint64_t*)SCLDN(pte);
    if (!(*(tmp + pte_id) & 1)) {
        return 0;
    }
    *phy_addr = tmp[pte_id];
    return 1;
}


uint64_t get_mapping(uint64_t pml_addr, uint64_t viraddr)
{
    #define SCLDN(vaddr) (((vaddr >> 12)) << 12)

    int pml4_id = (viraddr >> 39) & 0x1FF;
    int pdpte_id = (viraddr >> 30) & 0x1FF;
    int pde_id = (viraddr >> 21) & 0x1FF;
    int pte_id = (viraddr >> 12) & 0x1FF;
    int offset = viraddr & 0xFFF;

    uint64_t* tmp = (uint64_t*)SCLDN(pml_addr);
    uint64_t pdpte = tmp[pml4_id] + KERNBASE;

    tmp = (uint64_t*)SCLDN(pdpte);
    uint64_t pde = tmp[pdpte_id] + KERNBASE;

    tmp = (uint64_t*)SCLDN(pde);
    uint64_t pte = tmp[pde_id] + KERNBASE;

    tmp = (uint64_t*)SCLDN(pte);
    uint64_t page_addr = tmp[pte_id];

    uint64_t phy_addr = SCLDN(page_addr) + offset ;
    return phy_addr;
}

void set_mapping(uint64_t pml4_addr, uint64_t viraddr, uint64_t phyaddr, uint64_t flags)
{
    //pml4 offset
    uint64_t pml4_offset = (viraddr >> 39) & 0x1FF;
    //pdpte offset
    uint64_t pdpte_offset = ((viraddr >> 30) & 0x1FF);
    //pde offset
    uint64_t pde_offset = ((viraddr >> 21) & 0x1FF);
    //pte offset
    uint64_t pte_offset = ((viraddr >> 12) & 0x1FF);

    // First PML4 table
    uint64_t pml4_offset_addr = (uint64_t)(((uint64_t*)pml4_addr) + pml4_offset);
    uint64_t pdpte_phy =  create_dir_table(pml4_offset_addr);

    // PDPTE table
    uint64_t pdpte_v_addr = pdpte_phy + KERNBASE;
    uint64_t pdpte_offset_addr = (uint64_t) (((uint64_t*)pdpte_v_addr) + pdpte_offset);
    uint64_t pde_phy = create_dir_table(pdpte_offset_addr);

    //pde table
    uint64_t pde_v_addr = pde_phy + KERNBASE;
    uint64_t pde_offset_addr = (uint64_t)(((uint64_t*)pde_v_addr) +pde_offset);
    uint64_t pte_phy = create_dir_table(pde_offset_addr);

    //pte table
    uint64_t pte_v_addr=pte_phy + KERNBASE;
    uint64_t pte_offset_addr = (uint64_t)(((uint64_t*)pte_v_addr) +pte_offset);
    *((uint64_t*)pte_offset_addr) = phyaddr | flags;
}

uint64_t create_dir_table(uint64_t vir_addr)
{

    uint64_t phy_addr  = *((uint64_t*)vir_addr);
    if (!(phy_addr & 1))
    {
        uint64_t newPhyAddr = (uint64_t) fetch_free_page();
        *((uint64_t*)vir_addr) = newPhyAddr | 7;
        return newPhyAddr;
    }
    else
    {
        return (uint64_t)ScaleDown((uint64_t *)phy_addr);
    }
}

// func takes in virtual address of pml4
void clear_mapping(uint64_t pml4_addr) {
#define SCLDN(vaddr) (((vaddr >> 12)) << 12)

    // clear all pml entries except for 511th entry
    uint64_t* pml = (uint64_t *) SCLDN(pml4_addr);

    for (int i = 0; i < 511; i++) {
        // if entry not present
        if (!(pml[i] & 1)) {
            pml[i] = 0;
            continue;
        }
        // if pml entry present calculate pdpte
        uint64_t *pdpte = (uint64_t * )SCLDN((pml[i] + KERNBASE));

        for (int j = 0; j < 512; j++) {
            // if entry not present
            if (!(pdpte[j] & 1)) {
                pdpte[j] = 0;
                continue;
            }

            // if pdpte entry present calculate pde
            uint64_t *pde = (uint64_t * )SCLDN((pdpte[j] + KERNBASE));

            for (int k = 0; k < 512; k++) {
                // if entry not present
                if (!(pde[k] & 1)) {
                    pde[k] = 0;
                    continue;
                }

                // if pde entry present calculate pte
                uint64_t *pte = (uint64_t *)SCLDN((pde[k] + KERNBASE));

                for (int l = 0; l < 512; l++) {
                    // if entry not present
                    if (!(pte[l] & 1)) {
                        pte[l] = 0;
                        continue;
                    }

                    // if pte entry present, free page
                    uint64_t *pg_addr = (uint64_t *)SCLDN((pte[l] + KERNBASE));
                    free_page((void *) pg_addr);
                    pte[l] = 0;
                }

                // free pde page
                free_page((void *) SCLDN((pde[k] + KERNBASE)));
                pde[k] = 0;
            }

            // free pdpte page
            free_page((void *) SCLDN((pdpte[j] + KERNBASE)));
            pdpte[j] = 0;
        }

        // free pml page
        free_page((void *) SCLDN((pml[i] + KERNBASE)));
        pml[i] = 0;
    }
}


void create_page_list(uint64_t *physfree, uint32_t *modulep, uint64_t *mem_end)
{

    struct smap_t
    {
        uint64_t base, length;
        uint32_t type;
    }__attribute__((packed)) *smap;
    page_count = (uint64_t) mem_end / PAGE_SIZE;
    // Keeping buffer for page tables and pages array
    physfree = ScaleUp((uint64_t * )((uint64_t) physfree + sizeof(struct page) * page_count));

    // Traversing pages to assign free pages & page list array proper values
    memset((void *) physfree, 0, 5 * PAGE_SIZE);
    for (int page_num = 0; page_num < page_count; page_num++)
    {
        uint64_t page_start = page_num * PAGE_SIZE;
        uint64_t page_end = page_start + PAGE_SIZE - 1;

        // FIXME: add rest of the locations to free list once paging is done
        if (page_start < (uint64_t) physfree || page_end < (uint64_t) physfree)
        {
            pages[page_num].ref_count = 1;
        }
        else
        {
            int page_marked = 0;
            for (smap = (struct smap_t *) (modulep + 2);
                 smap < (struct smap_t *) ((char *) modulep + modulep[1] + 2 * 4); ++smap)
            {
                if (smap->type == 1 && smap->length != 0)
                {
                    if ((page_start < smap->base + smap->length && page_start > smap->base) &&
                        (page_end < smap->base + smap->length && page_end > smap->base))
                    {
                        page_marked = 1;
                        if (free_page_head == NULL)
                        {
                            pages[page_num].ref_count = 0;
                            free_page_head = &pages[page_num];
                            free_page_head->next = NULL;
                            free_page_end = free_page_head;
                        }
                        else
                        {
                            pages[page_num].ref_count = 0;
                            free_page_end->next = &pages[page_num];
                            free_page_end = &pages[page_num];
                            free_page_end->next = NULL;
                        }
                    }

                }
            }
            if (page_marked == 0)
            {
                pages[page_num].ref_count = 1;
            }
        }
    }
}

uint64_t * ScaleDown(uint64_t *addr)
{
    return (uint64_t * )((uint64_t) addr - ((uint64_t) addr % PAGE_SIZE));
}

uint64_t * ScaleUp(uint64_t *addr)
{
    if (((uint64_t) addr % PAGE_SIZE) != 0)
    {
        addr = (uint64_t * )((uint64_t) addr + (PAGE_SIZE - ((uint64_t) addr % PAGE_SIZE)));
    }
    return addr;
}

struct page *fetch_free_page()
{
    // Returns the physical address of free page
    // FIXME: handle no free page
    if ((free_page_head == NULL) || (free_page_head == free_page_end))
    {
        // return NULL;
        kprintf("Out of free pages!!!");
    }

    struct page *tmp = free_page_head;
    struct page *free_pg = (struct page *) getPA(tmp);
    struct page *free_pntr = (struct page *) get_viraddr((uint64_t) tmp);
    free_pntr->ref_count = 1;
    free_page_head = free_pntr->next;
    uint64_t *free_pg_vir = (uint64_t *) get_viraddr((uint64_t) free_pg);
    memset((void *) free_pg_vir, 0, PAGE_SIZE);

    return free_pg;
}