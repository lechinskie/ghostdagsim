#include "dag.h"

#include <queue>

void
Blockchain::AddBlock(const Block& block)
{
    bool is_orphan = false;
    for (int parent_id : block.parent_hashes)
    {
        if (blocks.find(parent_id) == blocks.end())
        {
            is_orphan = true;
            break;
        }
    }

    if (is_orphan)
    {
        orphans[block.block_id] = block;
        return;
    }

    blocks[block.block_id] = block;

    for (int parent_id : block.parent_hashes)
    {
        children[parent_id].insert(block.block_id);
    }

    for (int parent_id : block.parent_hashes)
    {
        tips.erase(parent_id);
    }
    tips.insert(block.block_id);

    std::set<int> blue_set = CalculateBlueSet(block.block_id);
    blocks[block.block_id].is_blue = (blue_set.find(block.block_id) != blue_set.end());
    blocks[block.block_id].blue_score = CalculateBlueScore(block.block_id, blue_set);
    int max_blue_parent = -1;
    int max_blue_score = -1;
    for (int parent_id : block.parent_hashes)
    {
        if (blocks[parent_id].blue_score > max_blue_score)
        {
            max_blue_score = blocks[parent_id].blue_score;
            max_blue_parent = parent_id;
        }
    }
    blocks[block.block_id].selected_parent = max_blue_parent;

    // Unorphan if needs
    std::vector<int> to_unorphan;
    for (auto& orphan_pair : orphans)
    {
        bool can_add = true;
        for (int parent_id : orphan_pair.second.parent_hashes)
        {
            if (blocks.find(parent_id) == blocks.end())
            {
                can_add = false;
                break;
            }
        }
        if (can_add)
        {
            to_unorphan.push_back(orphan_pair.first);
        }
    }

    for (int orphan_id : to_unorphan)
    {
        Block orphan_block = orphans[orphan_id];
        orphans.erase(orphan_id);
        AddBlock(orphan_block);
    }
}

std::set<int>
Blockchain::CalculateBlueSet(int block_id)
{
    return GreedyBlueSet(block_id);
}

std::set<int>
Blockchain::GreedyBlueSet(int block_id)
{
    std::set<int> blue;
    std::set<int> past = GetPast(block_id);

    if (past.empty())
    {
        blue.insert(block_id);
        return blue;
    }

    int max_blue_parent = -1;
    int max_blue_score = -1;

    for (int parent_id : blocks[block_id].parent_hashes)
    {
        if (blocks[parent_id].blue_score > max_blue_score)
        {
            max_blue_score = blocks[parent_id].blue_score;
            max_blue_parent = parent_id;
        }
    }

    if (max_blue_parent != -1)
    {
        std::set<int> parent_past = GetPast(max_blue_parent);
        for (int bid : parent_past)
        {
            if (blocks[bid].is_blue)
            {
                blue.insert(bid);
            }
        }
        if (blocks[max_blue_parent].is_blue)
        {
            blue.insert(max_blue_parent);
        }
    }

    for (int bid : past)
    {
        if (blue.find(bid) == blue.end())
        {
            std::set<int> test_blue = blue;
            test_blue.insert(bid);

            int anticone_size = 0;
            for (auto it1 = test_blue.begin(); it1 != test_blue.end(); ++it1)
            {
                auto it2 = it1;
                ++it2;
                for (; it2 != test_blue.end(); ++it2)
                {
                    std::set<int> anticone = GetAnticone(*it1, *it2);
                    for (int ac : anticone)
                    {
                        if (test_blue.find(ac) != test_blue.end())
                        {
                            anticone_size++;
                            if (anticone_size > ghostdag_k)
                            {
                                break;
                            }
                        }
                    }
                    if (anticone_size > ghostdag_k)
                    {
                        break;
                    }
                }
                if (anticone_size > ghostdag_k)
                {
                    break;
                }
            }

            if (anticone_size <= ghostdag_k)
            {
                blue.insert(bid);
            }
        }
    }

    // Check if block_id itself can be added to blue set
    int current_anticone_size = 0;
    for (auto it1 = blue.begin(); it1 != blue.end(); ++it1)
    {
        auto it2 = it1;
        ++it2;
        for (; it2 != blue.end(); ++it2)
        {
            std::set<int> anticone = GetAnticone(*it1, *it2);
            for (int ac : anticone)
            {
                if (blue.find(ac) != blue.end() || ac == block_id)
                {
                    current_anticone_size++;
                    if (current_anticone_size > ghostdag_k)
                    {
                        break;
                    }
                }
            }
            if (current_anticone_size > ghostdag_k)
            {
                break;
            }
        }
        if (current_anticone_size > ghostdag_k)
        {
            break;
        }
    }

    // Also check anticone between block_id and blue blocks
    for (int blue_block : blue)
    {
        std::set<int> anticone = GetAnticone(block_id, blue_block);
        for (int ac : anticone)
        {
            if (blue.find(ac) != blue.end())
            {
                current_anticone_size++;
                if (current_anticone_size > ghostdag_k)
                {
                    break;
                }
            }
        }
        if (current_anticone_size > ghostdag_k)
        {
            break;
        }
    }

    if (current_anticone_size <= ghostdag_k)
    {
        blue.insert(block_id);
    }

    return blue;
}

int
Blockchain::CalculateBlueScore(int block_id, const std::set<int>& blue_set)
{
    std::set<int> past = GetPast(block_id);
    int score = 0;

    for (int bid : past)
    {
        if (blue_set.find(bid) != blue_set.end())
        {
            score++;
        }
    }

    if (blue_set.find(block_id) != blue_set.end())
    {
        score++;
    }

    return score;
}

std::set<int>
Blockchain::GetPast(int block_id)
{
    std::set<int> past;
    std::queue<int> to_visit;

    if (blocks.find(block_id) == blocks.end())
    {
        return past;
    }

    for (int parent_id : blocks[block_id].parent_hashes)
    {
        to_visit.push(parent_id);
    }

    while (!to_visit.empty())
    {
        int current = to_visit.front();
        to_visit.pop();

        if (past.find(current) != past.end())
        {
            continue;
        }

        past.insert(current);

        if (blocks.find(current) != blocks.end())
        {
            for (int parent_id : blocks[current].parent_hashes)
            {
                if (past.find(parent_id) == past.end())
                {
                    to_visit.push(parent_id);
                }
            }
        }
    }

    return past;
}

std::set<int>
Blockchain::GetFuture(int block_id)
{
    std::set<int> future;
    std::queue<int> to_visit;

    if (children.find(block_id) != children.end())
    {
        for (int child_id : children[block_id])
        {
            to_visit.push(child_id);
        }
    }

    while (!to_visit.empty())
    {
        int current = to_visit.front();
        to_visit.pop();

        if (future.find(current) != future.end())
        {
            continue;
        }

        future.insert(current);

        if (children.find(current) != children.end())
        {
            for (int child_id : children[current])
            {
                if (future.find(child_id) == future.end())
                {
                    to_visit.push(child_id);
                }
            }
        }
    }

    return future;
}

std::set<int>
Blockchain::GetAnticone(int block_id, int other_block_id)
{
    std::set<int> anticone;

    std::set<int> past_1 = GetPast(block_id);
    std::set<int> future_1 = GetFuture(block_id);

    if (past_1.find(other_block_id) != past_1.end() ||
        future_1.find(other_block_id) != future_1.end())
    {
        return anticone; // Empty set
    }

    std::set<int> past_2 = GetPast(other_block_id);
    std::set<int> future_2 = GetFuture(other_block_id);

    for (const auto& pair : blocks)
    {
        int bid = pair.first;
        if (bid == block_id || bid == other_block_id)
        {
            continue;
        }

        bool in_past_or_future_1 =
            (past_1.find(bid) != past_1.end()) || (future_1.find(bid) != future_1.end());
        bool in_past_or_future_2 =
            (past_2.find(bid) != past_2.end()) || (future_2.find(bid) != future_2.end());

        if (!in_past_or_future_1 && !in_past_or_future_2)
        {
            anticone.insert(bid);
        }
    }

    return anticone;
}

int
Blockchain::SelectTip()
{
    if (tips.empty())
    {
        return -1;
    }

    int max_blue_score = -1;
    int selected_tip = -1;

    for (int tip : tips)
    {
        if (blocks[tip].blue_score > max_blue_score)
        {
            max_blue_score = blocks[tip].blue_score;
            selected_tip = tip;
        }
        else if (blocks[tip].blue_score == max_blue_score && selected_tip != -1)
        {
            // Use block_id as tie-breaker for determinism
            if (tip < selected_tip)
            {
                selected_tip = tip;
            }
        }
    }

    return selected_tip;
}

std::vector<int>
Blockchain::ComputeGHOSTDAGOrdering()
{
    std::vector<int> ordering;

    std::map<int, int> in_degree;
    std::set<int> visited;

    for (const auto& pair : blocks)
    {
        in_degree[pair.first] = pair.second.parent_hashes.size();
    }

    auto cmp = [this](int a, int b) {
        if (blocks[a].blue_score != blocks[b].blue_score)
        {
            return blocks[a].blue_score < blocks[b].blue_score;
        }
        if (blocks[a].time_created != blocks[b].time_created)
        {
            return blocks[a].time_created > blocks[b].time_created;
        }
        return a > b;
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    for (const auto& pair : in_degree)
    {
        if (pair.second == 0)
        {
            pq.push(pair.first);
        }
    }

    while (!pq.empty())
    {
        int current = pq.top();
        pq.pop();

        ordering.push_back(current);
        visited.insert(current);

        if (children.find(current) != children.end())
        {
            for (int child : children[current])
            {
                in_degree[child]--;
                if (in_degree[child] == 0 && visited.find(child) == visited.end())
                {
                    pq.push(child);
                }
            }
        }
    }

    return ordering;
}

bool
Blockchain::IsKCluster(const std::set<int>& blue_set)
{
    for (int b1 : blue_set)
    {
        for (int b2 : blue_set)
        {
            if (b1 >= b2)
            {
                continue;
            }

            std::set<int> anticone = GetAnticone(b1, b2);
            int count = 0;
            for (int bid : anticone)
            {
                if (blue_set.find(bid) != blue_set.end())
                {
                    count++;
                }
            }

            if (count > ghostdag_k)
            {
                return false;
            }
        }
    }
    return true;
}

int
Blockchain::GetDagWidth() const
{
    return static_cast<int>(tips.size());
}

bool
Blockchain::HasBlock(int block_id) const
{
    return blocks.find(block_id) != blocks.end();
}

bool
Blockchain::IsRed(int block_id) const
{
    auto it = blocks.find(block_id);
    if (it != blocks.end())
    {
        return !it->second.is_blue;
    }
    return false;
}

bool
Blockchain::IsOrphan(int block_id) const
{
    return orphans.find(block_id) != orphans.end();
}

std::vector<const Block*>
Blockchain::GetChildrenPointers(const Block& block)
{
    std::vector<const Block*> children_pointers;
    auto it = children.find(block.block_id);
    if (it != children.end())
    {
        for (int child_id : it->second)
        {
            auto block_it = blocks.find(child_id);
            if (block_it != blocks.end())
            {
                children_pointers.push_back(&(block_it->second));
            }
        }
    }
    return children_pointers;
}

std::vector<const Block*>
Blockchain::GetParentsPointers(const Block& block)
{
    std::vector<const Block*> parent_pointers;
    for (int parent_id : block.parent_hashes)
    {
        auto block_it = blocks.find(parent_id);
        if (block_it != blocks.end())
        {
            parent_pointers.push_back(&(block_it->second));
        }
    }
    return parent_pointers;
}
