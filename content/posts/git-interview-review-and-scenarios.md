---
{
  "title": "Git 面试速查与场景题",
  "slug": "git-interview-review-and-scenarios",
  "date": "2026-08-08",
  "updated": "2026-08-08",
  "excerpt": "从工作区、暂存区和版本库出发，系统梳理 Git 常用命令、撤销策略、分支协作、远程同步和冲突解决。",
  "category": { "name": "工程实践", "slug": "engineering" },
  "tags": [
    { "name": "Git", "slug": "git" },
    { "name": "面试复习", "slug": "interview" },
    { "name": "工程实践", "slug": "engineering" }
  ],
  "featured": false,
  "draft": false,
  "seoDescription": "Git 面试复习文章，覆盖工作区与暂存区、reset 与 revert、分支协作、远程提交竞争、merge/rebase 冲突解决和常见排错场景。"
}
---

> 适合面试前快速复习。重点覆盖本地版本管理、撤销修改、分支协作、远程仓库和常见排错场景。

## 一、先记住 Git 的三层结构

Git 的一次提交通常会经过下面三层：

```text
工作区 Working Tree  --git add-->  暂存区 Staging Area  --git commit-->  本地版本库 Repository
      |                                  |                                  |
   正在编辑的文件                    下一次提交的快照                    已提交的历史记录
```

- **工作区**：当前目录中实际编辑的文件。
- **暂存区**：`.git/index`，保存“下一次 commit 准备提交的内容”。
- **本地版本库**：`.git` 目录中的对象和提交历史。
- **HEAD**：当前检出的提交，通常间接指向当前分支的最新提交。
- **分支**：指向某个提交的可移动指针。新仓库的默认分支可能叫 `main`，也可能由配置决定，不要默认认为一定是 `master`。

最重要的一句话：

> `git add` 不是提交，它只是把工作区的修改放入暂存区；`git commit` 才是把暂存区内容保存为一个新的提交。

## 二、从零创建一次本地提交

### 1. 初始化仓库

```bash
git init
```

在当前目录创建 `.git`，开始由 Git 跟踪版本。`.git` 是版本库本身，不属于业务代码的工作区。

### 2. 查看状态

```bash
git status
```

它可以回答三个问题：

- 哪些文件只在工作区被修改，还没有 `add`？
- 哪些修改已经进入暂存区，等待提交？
- 哪些文件是未跟踪文件（`untracked`）？

### 3. 添加到暂存区

```bash
git add README.md       # 添加一个文件
git add src/             # 添加一个目录
git add .                # 添加当前目录下的修改和新文件，使用前确认范围
git add -p               # 交互式选择某个文件中的部分修改
```

### 4. 提交

```bash
git commit -m "docs: add Git interview notes"
```

`-m` 后面的内容是提交说明。提交说明应描述这次提交完成了什么，例如 `fix: handle empty input`，而不是写成 `update`、`test` 这类无法表达意图的词。

常见查看方式：

```bash
git log --oneline --decorate --graph --all
git show <commit-id>
```

## 三、如何查看修改

```bash
git diff                 # 工作区 vs 暂存区：还没有 add 的修改
git diff --cached        # 暂存区 vs 最近一次提交：已经 add 的修改
git diff HEAD            # 工作区 + 暂存区 vs 最近一次提交
git diff <commit1> <commit2>
```

理解这几个命令的关键是比较对象：

| 命令 | 比较内容 | 用途 |
| --- | --- | --- |
| `git diff` | 工作区与暂存区 | 检查还未暂存的修改 |
| `git diff --cached` | 暂存区与 `HEAD` | 检查下一次提交将包含什么 |
| `git diff HEAD` | 工作区与 `HEAD` | 查看当前所有未提交修改 |

## 四、撤销修改：按修改所处位置处理

### 情况 1：只修改了工作区，还没有 `git add`

现代写法：

```bash
git restore README.md
```

这会用暂存区的版本覆盖工作区文件，未保存的工作区修改会丢失。

旧写法：

```bash
git checkout -- README.md
```

面试时可以说明：`checkout -- <file>` 仍可能在旧项目中看到，但现代 Git 更推荐 `git restore`，因为 `checkout` 同时承担切换分支和恢复文件两种职责，语义不够清晰。

### 情况 2：已经 `git add`，但还没有 `commit`

只想取消暂存、保留文件修改：

```bash
git restore --staged README.md
# 旧写法：git reset HEAD README.md
```

此时文件会从“暂存区”退回“工作区”，内容不会消失。

如果想连工作区修改也一起丢弃：

```bash
git restore --staged --worktree README.md
```

### 情况 3：已经 `add` 并 `commit`

此时修改已经进入提交历史，不能再靠 `git restore` 直接撤销，需要根据是否已经共享给别人选择方式：

```bash
# 本地还未推送，想移动分支指针
git reset --soft HEAD~1   # 保留修改，并放入暂存区
git reset HEAD~1           # 保留修改，但取消暂存（等价于 --mixed）
git reset --hard HEAD~1    # 工作区、暂存区一起丢弃，危险

# 已推送或公共分支，推荐生成一个反向提交
git revert <commit-id>
```

区别：

- `reset` 改写当前分支历史，适合尚未共享的本地提交。
- `revert` 不删除历史，而是新增一个“抵消指定提交”的提交，适合已经推送的公共分支。
- `reset --hard` 可能造成未提交修改永久丢失，执行前必须确认 `git status` 和目标提交。

## 五、删除、移动和忽略文件

```bash
git rm test.py                 # 删除文件，并把删除操作放入暂存区
git rm --cached config.local   # 只从 Git 跟踪中移除，保留本地文件
git mv old_name.cpp new_name.cpp
```

`.gitignore` 用于忽略不应提交的文件，例如：

```gitignore
build/
*.log
.env
*.user
```

注意：`.gitignore` 只对“尚未被 Git 跟踪”的文件生效。如果文件已经提交过，需要先执行 `git rm --cached <file>`，再提交忽略规则。

## 六、分支：隔离开发和合并代码

```bash
git branch                         # 查看本地分支
git switch -c feature/login       # 创建并切换到新分支
git switch main                   # 切换分支
git branch -d feature/login       # 删除已合并分支
git merge feature/login           # 将分支合并到当前分支
```

旧项目常见写法是 `git checkout -b feature/login` 和 `git checkout main`；现代 Git 推荐 `switch` 表达分支操作。

### 合并冲突处理流程

1. 执行 `git merge` 或 `git pull` 后发现冲突。
2. `git status` 找出冲突文件。
3. 手动处理 `<<<<<<<`、`=======`、`>>>>>>>` 标记，保留正确内容。
4. `git add <冲突文件>` 标记冲突已解决。
5. `git commit` 完成合并提交。

如果决定放弃本次合并：

```bash
git merge --abort
```

## 七、远程仓库与团队协作

```bash
git remote -v
git remote add origin <仓库地址>
git fetch origin                 # 下载远程对象和分支信息，不修改当前工作区
git pull --rebase origin main    # fetch + rebase
git push -u origin main          # 首次推送并建立上游分支
```

### `fetch`、`pull`、`push` 的区别

- `fetch`：只把远程最新历史下载到本地，不自动合并。
- `pull`：通常是 `fetch` 加 `merge`，也可以使用 `--rebase` 改为变基。
- `push`：把本地提交上传到远程仓库。

### merge 和 rebase

- `merge` 保留真实分支结构，可能产生合并提交，适合强调历史完整性。
- `rebase` 将当前分支提交重新接到目标分支后面，历史更线性，但会改写提交哈希。
- 已经推送且被多人依赖的分支不要随意 `rebase`；如果确实需要强制推送，优先使用 `git push --force-with-lease`，不要习惯性使用 `--force`。

### 两个人几乎同时开发并提交时会发生什么？

假设远程 `main` 当前是 `A`：

```text
你本地： A -- B
同事远程：A -- C
```

同事先把 `C` 推送到远程后，你再执行 `git push`，通常会看到 `rejected`、`non-fast-forward` 或“远程包含本地没有的提交”等提示。原因是：远程分支已经向前移动，而你的本地分支不是远程分支的直接后继。Git 默认拒绝覆盖同事的提交。

推荐处理流程如下：

```bash
# 1. 确认本地没有未保存的工作
git status

# 2. 获取远程最新历史
git fetch origin

# 3. 把自己的提交放到同事提交之后（团队允许 rebase 时）
git rebase origin/main

# 4. 解决冲突并完成 rebase 后再推送
git push origin main
```

如果团队不使用 rebase，也可以采用合并：

```bash
git fetch origin
git merge origin/main
# 解决冲突后提交合并结果
git push origin main
```

不要一看到 push 被拒绝就执行 `git push --force`。这可能直接覆盖远程新提交。只有在明确确认分支允许改写历史时，才考虑：

```bash
git push --force-with-lease
```

`--force-with-lease` 会在远程分支仍然是自己预期状态时才强推，比无条件的 `--force` 更安全，但它也不是解决普通 push 冲突的首选方案。

### 冲突标记和解决步骤

当两个提交修改了同一文件的同一片段，Git 无法自动判断应该保留哪一份内容，就会产生冲突。文件中通常会出现：

```text
<<<<<<< HEAD
当前分支的内容
=======
正在合入的分支的内容
>>>>>>> other-branch
```

处理冲突的通用流程：

1. 执行 `git status`，确认哪些文件处于 `unmerged` 状态。
2. 打开冲突文件，结合业务逻辑编辑最终内容，删除 `<<<<<<<`、`=======`、`>>>>>>>` 标记。
3. 运行编译、单元测试或必要的手工验证，确认不是简单地“随便选一边”。
4. 用 `git add <冲突文件>` 标记该文件已经解决。
5. 根据当前操作继续：

```bash
# merge 冲突
git commit

# rebase 冲突
git rebase --continue
```

如果发现这次操作不应该继续，可以安全退出：

```bash
git merge --abort
git rebase --abort
```

需要注意：`git add` 在这里的含义不是“提交代码”，而是告诉 Git“我已经处理完这个冲突”。仍然必须用 `git commit` 或 `git rebase --continue` 完成当前流程。

## 八、面试高频场景题

### 场景 1：我改了文件，但 `git commit` 提示没有内容

检查：

```bash
git status
git diff
```

通常是因为只修改了工作区，还没有 `git add`。正确流程是：

```bash
git add <file>
git commit -m "describe the change"
```

### 场景 2：只想提交一个文件中的一部分修改

```bash
git add -p <file>
```

逐块选择 `y`（暂存）、`n`（不暂存）、`s`（拆分），避免把无关调试代码一起提交。

### 场景 3：最后一次提交说明写错了，还没有推送

```bash
git commit --amend -m "correct message"
```

如果还要补充文件：先 `git add`，再执行 `git commit --amend`。已经推送的提交不应随意 amend 后强推。

### 场景 4：误把密码文件提交了

先立即撤销提交并从跟踪中移除：

```bash
git rm --cached .env
echo .env >> .gitignore
git commit -m "chore: stop tracking local secrets"
```

但这只能阻止后续跟踪，秘密已经进入历史时仍需立刻更换密钥，并使用 `git filter-repo` 等工具清理历史；不能把“删除当前文件”当作凭证泄露已解决。

### 场景 5：我准备提交时，同事先一步提交并推送了

这是最常见的协作场景。你的本地和远程各有新提交，直接 push 会被拒绝。先获取远程历史，再选择团队约定的整合方式：

```bash
git status
git fetch origin
git rebase origin/main
# 如果有冲突：编辑文件 -> git add <file> -> git rebase --continue
git push
```

如果团队约定使用 merge：

```bash
git fetch origin
git merge origin/main
# 如果有冲突：编辑文件 -> git add <file> -> git commit
git push
```

回答面试题时可以概括为：**先 fetch 获取远程提交，再 rebase 或 merge 整合，解决冲突并测试，最后 push；不能直接强推覆盖远程。**

### 场景 6：merge/rebase 时发生冲突，应该怎么做？

核心命令顺序如下：

```bash
git status                  # 找到冲突文件
# 手动编辑并删除冲突标记
git diff                    # 检查最终内容
git add <冲突文件>
git rebase --continue       # 如果原来执行的是 rebase
# 或 git commit              # 如果原来执行的是 merge
```

解决后必须运行测试。若发现方向不对，使用 `git rebase --abort` 或 `git merge --abort` 回到操作开始前的状态。

### 场景 7：需要临时切换分支，但当前修改还不能提交

```bash
git stash push -m "wip: login form"
git switch main
git switch feature/login
git stash pop
```

也可以使用 `git stash list` 查看多个暂存现场。长期工作不建议过度依赖 stash，能形成逻辑完整的临时提交时，临时提交通常更容易追踪。

### 场景 8：已经 push 的提交想撤销，应该 reset 还是 revert？

公共分支使用：

```bash
git revert <commit-id>
git push
```

因为 `revert` 保留原历史并新增反向提交，不会让其他人的本地分支失去共同祖先。`reset` 适合尚未共享的本地历史整理。

### 场景 9：如何找出是谁引入了某一行代码？

```bash
git blame -L 20,30 src/user.cpp
git log -S "关键字符串" --oneline -- src/user.cpp
git log -G "正则表达式" --oneline -- src/user.cpp
```

`blame` 定位每一行最后由哪个提交修改；`log -S` 查找字符串增删，`log -G` 按正则匹配差异。

### 场景 10：线上出现回归，如何快速定位引入问题的提交？

使用二分查找：

```bash
git bisect start
git bisect bad                  # 当前版本有问题
git bisect good <已知正常提交>
# Git 切换到中间提交，运行测试后执行 good 或 bad
git bisect good
git bisect bad
git bisect reset
```

## 九、一套可背诵的日常流程

```bash
git switch -c feature/my-change
git status
git diff
git add -p
git diff --cached
git commit -m "feat: implement my change"
git fetch origin
git rebase origin/main
git push -u origin feature/my-change
```

面试回答时，优先说明“当前修改在哪一层、是否已经共享给别人”，再决定使用 `restore`、`reset`、`revert`、`merge` 还是 `rebase`。这比死记命令更重要。

## 十、最后的速记口诀

```text
改文件：工作区
git add：进暂存区
git commit：进本地历史
git push：进远程仓库

未 add：restore 文件
已 add：restore --staged 文件
已 commit 且未共享：reset
已 push 或公共分支：revert
```
