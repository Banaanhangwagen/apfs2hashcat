#include <cassert>

#include "ApfsContainer.h"
#include "DiskStruct.h"
#include "BlockDumper.h"
#include "CheckPointMap.h"

CheckPointMap::CheckPointMap(ApfsContainer& container) : m_container(container)
{
	m_cpm_oid = 0;
	m_blksize = 0;
}

CheckPointMap::~CheckPointMap()
{
}

bool CheckPointMap::Init(paddr_t desc_base, uint32_t desc_blocks, uint32_t desc_index, uint32_t desc_len)
{
	uint32_t n;
	const checkpoint_map_phys_t *cpm;

	// The high bit of nx_xp_desc_blocks marks a B-tree descriptor area (unsupported here);
	// the low 31 bits are the ring-buffer size in blocks.
	desc_blocks &= 0x7FFFFFFF;
	if (desc_blocks == 0 || desc_len == 0)
		return false;

	// The current checkpoint occupies desc_len blocks starting at desc_index and WRAPPING
	// around the ring buffer of size desc_blocks. The last block is the container superblock;
	// the preceding (desc_len - 1) blocks are the checkpoint map(s).
	const uint32_t blk_count = desc_len - 1;

	m_blksize = m_container.GetBlocksize();
	m_cpm_data.resize(m_blksize * blk_count);

	for (n = 0; n < blk_count; n++)
	{
		const paddr_t paddr = desc_base + ((desc_index + n) % desc_blocks);

		if (!m_container.ReadAndVerifyHeaderBlock(m_cpm_data.data() + n * m_blksize, paddr))
		{
			m_cpm_data.clear();
			return false;
		}

		cpm = reinterpret_cast<const checkpoint_map_phys_t *>(m_cpm_data.data() + m_blksize * n);

		if ((cpm->cpm_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_CHECKPOINT_MAP)
		{
			m_cpm_data.clear();
			return false;
		}
	}

	m_cpm_oid = desc_base + desc_index;

	return true;
}

bool CheckPointMap::Lookup(omap_res_t & res, oid_t oid, xid_t xid)
{
	uint32_t blk_offs;
	uint32_t k;
	uint32_t cnt;
	const checkpoint_map_phys_t * cpm;

	(void)xid;

	for (blk_offs = 0; blk_offs < m_cpm_data.size(); blk_offs += m_blksize)
	{
		cpm = reinterpret_cast<const checkpoint_map_phys_t *>(m_cpm_data.data() + blk_offs);

		cnt = cpm->cpm_count;

		for (k = 0; k < cnt; k++)
		{
			if (oid == cpm->cpm_map[k].cpm_oid)
			{
				res.flags = 0;
				res.size = cpm->cpm_map[k].cpm_size;
				res.paddr = cpm->cpm_map[k].cpm_paddr;

				return true;
			}
		}
	}

	return false;
}

void CheckPointMap::dump(BlockDumper& bd)
{
	bd.DumpNode(m_cpm_data.data(), m_cpm_oid);
}
