#include "dag.h"

#include <queue>

void
Blockchain::AddBlock(const Block& block)
{
    blocks[block.block_id] = block;

    for (int parentId : block.parent_hashes)
    {
        children[parentId].insert(block.block_id);
    }

    for (int parentId : block.parent_hashes)
    {
        tips.erase(parentId);
    }
    tips.insert(block.block_id);

    std::set<int> blueSet = CalculateBlueSet(block.block_id);
    blocks[block.block_id].is_blue = (blueSet.find(block.block_id) != blueSet.end());
    blocks[block.block_id].blue_score = CalculateBlueScore(block.block_id, blueSet);
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

    int maxBlueParent = -1;
    int maxBlueScore = -1;

    for (int parentId : blocks[block_id].parent_hashes)
    {
        if (blocks[parentId].blue_score > maxBlueScore)
        {
            maxBlueScore = blocks[parentId].blue_score;
            maxBlueParent = parentId;
        }
    }

    if (maxBlueParent != -1)
    {
        std::set<int> parentPast = GetPast(maxBlueParent);
        for (int bid : parentPast)
        {
            if (blocks[bid].is_blue)
            {
                blue.insert(bid);
            }
        }
        if (blocks[maxBlueParent].is_blue)
        {
            blue.insert(maxBlueParent);
        }
    }

    for (int bid : past)
    {
        if (blue.find(bid) == blue.end())
        {
            std::set<int> testBlue = blue;
            testBlue.insert(bid);

            int anticoneSize = 0;
            for (int blueBlock : testBlue)
            {
                std::set<int> anticone = GetAnticone(bid, blueBlock);
                for (int ac : anticone)
                {
                    if (testBlue.find(ac) != testBlue.end())
                    {
                        anticoneSize++;
                    }
                }
            }

            if (anticoneSize <= ghostdag_k)
            {
                blue.insert(bid);
            }
        }
    }

    int currentAnticoneSize = 0;
    for (int blueBlock : blue)
    {
        std::set<int> anticone = GetAnticone(block_id, blueBlock);
        for (int ac : anticone)
        {
            if (blue.find(ac) != blue.end())
            {
                currentAnticoneSize++;
            }
        }
    }

    if (currentAnticoneSize <= ghostdag_k)
    {
        blue.insert(block_id);
    }

    return blue;
}

int
Blockchain::CalculateBlueScore(int block_id, const std::set<int>& blueSet)
{
    std::set<int> past = GetPast(block_id);
    int score = 0;

    for (int bid : past)
    {
        if (blueSet.find(bid) != blueSet.end())
        {
            score++;
        }
    }

    if (blueSet.find(block_id) != blueSet.end())
    {
        score++;
    }

    return score;
}

std::set<int>
Blockchain::GetPast(int block_id)
{
    std::set<int> past;
    std::queue<int> toVisit;

    if (blocks.find(block_id) == blocks.end())
    {
        return past;
    }

    for (int parentId : blocks[block_id].parent_hashes)
    {
        toVisit.push(parentId);
    }

    while (!toVisit.empty())
    {
        int current = toVisit.front();
        toVisit.pop();

        if (past.find(current) != past.end())
        {
            continue;
        }

        past.insert(current);

        if (blocks.find(current) != blocks.end())
        {
            for (int parentId : blocks[current].parent_hashes)
            {
                if (past.find(parentId) == past.end())
                {
                    toVisit.push(parentId);
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
    std::queue<int> toVisit;

    if (children.find(block_id) != children.end())
    {
        for (int childId : children[block_id])
        {
            toVisit.push(childId);
        }
    }

    while (!toVisit.empty())
    {
        int current = toVisit.front();
        toVisit.pop();

        if (future.find(current) != future.end())
        {
            continue;
        }

        future.insert(current);

        if (children.find(current) != children.end())
        {
            for (int childId : children[current])
            {
                if (future.find(childId) == future.end())
                {
                    toVisit.push(childId);
                }
            }
        }
    }

    return future;
}

std::set<int>
Blockchain::GetAnticone(int block_id, int otherBlockId)
{
    std::set<int> anticone;
    std::set<int> past1 = GetPast(block_id);
    std::set<int> future1 = GetFuture(block_id);
    std::set<int> past2 = GetPast(otherBlockId);
    std::set<int> future2 = GetFuture(otherBlockId);

    for (const auto& pair : blocks)
    {
        int bid = pair.first;
        if (bid == block_id || bid == otherBlockId)
        {
            continue;
        }

        bool inPastOrFuture1 =
            (past1.find(bid) != past1.end()) || (future1.find(bid) != future1.end());
        bool inPastOrFuture2 =
            (past2.find(bid) != past2.end()) || (future2.find(bid) != future2.end());

        if (!inPastOrFuture1 && !inPastOrFuture2)
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

    int maxBlueScore = -1;
    int selectedTip = -1;

    for (int tip : tips)
    {
        if (blocks[tip].blue_score > maxBlueScore)
        {
            maxBlueScore = blocks[tip].blue_score;
            selectedTip = tip;
        }
    }

    return selectedTip;
}

std::vector<int>
Blockchain::ComputeGHOSTDAGOrdering()
{
    std::vector<int> ordering;

    std::map<int, int> inDegree;
    std::set<int> visited;

    for (const auto& pair : blocks)
    {
        inDegree[pair.first] = pair.second.parent_hashes.size();
    }

    auto cmp = [this](int a, int b) {
        if (blocks[a].blue_score != blocks[b].blue_score)
        {
            return blocks[a].blue_score < blocks[b].blue_score;
        }
        return blocks[a].time_created > blocks[b].time_created;
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    for (const auto& pair : inDegree)
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
                inDegree[child]--;
                if (inDegree[child] == 0 && visited.find(child) == visited.end())
                {
                    pq.push(child);
                }
            }
        }
    }

    return ordering;
}

bool
Blockchain::IsKCluster(const std::set<int>& blueSet)
{
    for (int b1 : blueSet)
    {
        for (int b2 : blueSet)
        {
            if (b1 >= b2)
            {
                continue;
            }

            std::set<int> anticone = GetAnticone(b1, b2);
            int count = 0;
            for (int bid : anticone)
            {
                if (blueSet.find(bid) != blueSet.end())
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
