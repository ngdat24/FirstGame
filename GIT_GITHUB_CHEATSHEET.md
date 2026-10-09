# Git & GitHub Cheatsheet cho FirstGame

Tài liệu nhanh cho quy trình làm việc với VS Code, Git và GitHub.

## 1. Mô hình cần nhớ

```text
File trong VS Code
        |
        v
Repository local trên máy
        |  git push
        v
Repository trên GitHub
```

- **Working tree**: các file đang mở/sửa trên máy.
- **Commit**: một mốc phiên bản được lưu trong Git local.
- **Branch**: một dòng phát triển riêng.
- **Remote**: repository trên GitHub, thường có tên `origin`.
- **Pull Request (PR)**: đề nghị nhập branch tính năng vào branch đích, thường là `main`.

## 2. Graph trong VS Code

- Mỗi chấm là một **commit**.
- Đường nối thể hiện thứ tự và quan hệ giữa các commit.
- `main`: branch chính.
- `origin/main`: thông tin local về branch `main` trên GitHub.
- `feature-branch`: branch đang phát triển tính năng.
- `Merge pull request`: điểm GitHub nhập branch tính năng vào `main`.

Ví dụ:

```text
main:    A ── B ───────── M
                  ╲      ╱
feature:           C ── D
```

- `A`, `B`: lịch sử cũ của `main`.
- `C`, `D`: các commit trên branch tính năng.
- `M`: merge commit sau khi PR được chấp nhận.

## 3. Kiểm tra trạng thái hiện tại

Mở Terminal trong đúng thư mục repository rồi chạy:

```powershell
git status
git branch --show-current
git branch -vv
git log --oneline --graph --decorate --all
```

Kết quả tốt thường có:

```text
nothing to commit, working tree clean
```

và branch local đang theo dõi branch tương ứng trên GitHub.

## 4. Các lệnh cơ bản

### Xem thay đổi

```powershell
git status
git diff
git diff --cached
```

- `git status`: file nào đã sửa/chưa theo dõi.
- `git diff`: thay đổi chưa được đưa vào commit.
- `git diff --cached`: thay đổi đã được `git add`.

### Chuẩn bị và lưu phiên bản

```powershell
git add .
git commit -m "Describe the change"
```

- `git add .`: đưa thay đổi vào vùng staging.
- `git commit`: tạo một mốc phiên bản local.
- Commit chưa xuất hiện trên GitHub cho đến khi `git push`.

### Đồng bộ với GitHub

```powershell
git push
git pull
```

- `git push`: đẩy commit local lên GitHub.
- `git pull`: lấy thay đổi từ GitHub và nhập vào branch hiện tại.
- `git pull` gần tương đương với `git fetch` rồi `git merge`.

## 5. Quy trình tạo tính năng mới

### Bước 1: cập nhật `main`

```powershell
git switch main
git pull origin main
```

### Bước 2: tạo branch riêng

```powershell
git switch -c add-new-feature
```

Tên branch nên mô tả mục tiêu:

```text
add-card-statistics
fix-rent-calculation
add-godot-ui
```

### Bước 3: sửa và kiểm thử

Với FirstGame:

```powershell
g++ -std=c++17 -Wall -Wextra -O2 `
  HUST_MONOPOLY\tests.cpp HUST_MONOPOLY\GameLogic.cpp `
  -o HUST_MONOPOLY\tests.exe

.\HUST_MONOPOLY\tests.exe
```

Chạy simulation:

```powershell
g++ -std=c++17 -Wall -Wextra -O2 `
  HUST_MONOPOLY\main.cpp HUST_MONOPOLY\GameLogic.cpp `
  -o HUST_MONOPOLY\sim.exe

.\HUST_MONOPOLY\sim.exe --games 2000 --seed 7
```

### Bước 4: commit và push

```powershell
git add .
git commit -m "Describe the feature"
git push -u origin add-new-feature
```

Sau lần push đầu tiên, các lần sau chỉ cần:

```powershell
git push
```

## 6. Pull Request là gì?

Pull Request là đề nghị:

> Nhập các commit từ branch tính năng vào branch đích.

PR cho phép:

- xem file thay đổi;
- xem diff từng dòng;
- review và bình luận;
- chạy test/CI;
- kiểm tra conflict;
- quyết định merge hoặc đóng PR.

Quy trình trên GitHub:

1. Push branch tính năng.
2. Mở repository `ngdat24/FirstGame`.
3. Chọn **Compare & pull request**.
4. Chọn:
   - **base**: `main`;
   - **compare**: branch tính năng.
5. Kiểm tra tab **Files changed**.
6. Tạo PR.
7. Review và chạy test.
8. Bấm **Merge pull request** khi mọi thứ ổn.

## 7. Merge Pull Request

Khi merge PR, GitHub đưa code từ branch tính năng vào `main`.

Có ba kiểu thường gặp:

### Merge commit

Giữ lịch sử branch và tạo thêm commit merge.

### Squash and merge

Gộp toàn bộ commit của PR thành một commit trên `main`.

### Rebase and merge

Đặt các commit của branch nối thẳng sau `main`, không tạo merge commit.

Với dự án cá nhân hoặc nhóm nhỏ, **Squash and merge** thường giúp lịch sử `main` gọn hơn.

## 8. Sau khi PR được merge

PR được merge trên GitHub không tự cập nhật thư mục local. Chạy:

```powershell
git switch main
git pull origin main
git status
```

Nếu branch tính năng không còn dùng:

```powershell
git branch -d add-new-feature
git push origin --delete add-new-feature
```

Chỉ xóa branch sau khi chắc chắn PR đã merge và không cần branch đó nữa.

## 9. Xử lý conflict

Conflict xảy ra khi hai branch cùng sửa một đoạn code.

Git có thể chèn:

```cpp
<<<<<<< HEAD
code của branch hiện tại
=======
code từ branch khác
>>>>>>> origin/main
```

Các bước xử lý:

1. Mở file bị conflict.
2. Chọn nội dung đúng hoặc kết hợp hai phần.
3. Xóa các dòng `<<<<<<<`, `=======`, `>>>>>>>`.
4. Lưu file.
5. Chạy test.
6. Đánh dấu đã xử lý:

```powershell
git add path\to\conflicted-file.cpp
```

7. Hoàn tất merge:

```powershell
git commit
```

Nếu muốn hủy quá trình merge chưa hoàn tất:

```powershell
git merge --abort
```

## 10. Đồng bộ với VS Code

Mở đúng thư mục worktree/repository trong VS Code. Kiểm tra:

```powershell
git branch --show-current
git status
```

Trong Source Control của VS Code:

- danh sách **Changes** là file đã sửa;
- dấu `+`/staging là tương đương `git add`;
- nút **Commit** là tương đương `git commit`;
- **Sync Changes** thường thực hiện pull/push;
- Graph hiển thị lịch sử commit và branch.

Lưu ý: Copilot có thể làm việc trong một worktree riêng. Nếu VS Code mở thư mục khác, bạn sẽ không thấy đúng thay đổi của Copilot. Hãy kiểm tra đường dẫn repository bằng:

```powershell
git rev-parse --show-toplevel
```

## 11. Quy trình của dự án FirstGame

```text
Claude thiết kế và viết gameplay
        |
        v
Copilot tích hợp vào repository
        |
        v
Copilot build, test, simulation
        |
        v
Copilot commit và push branch
        |
        v
Review trên GitHub/VS Code
        |
        v
Pull Request
        |
        v
Merge vào main
        |
        v
git switch main
git pull origin main
```

## 12. Bảng lệnh nhớ nhanh

| Mục tiêu | Lệnh |
|---|---|
| Xem trạng thái | `git status` |
| Xem branch hiện tại | `git branch --show-current` |
| Xem graph | `git log --oneline --graph --decorate --all` |
| Xem thay đổi | `git diff` |
| Stage mọi file | `git add .` |
| Tạo commit | `git commit -m "message"` |
| Đẩy lên GitHub | `git push` |
| Lấy code mới | `git pull` |
| Tạo branch | `git switch -c branch-name` |
| Chuyển branch | `git switch branch-name` |
| Xem repository root | `git rev-parse --show-toplevel` |
| Hủy merge conflict đang xử lý | `git merge --abort` |

## 13. Checklist trước khi merge PR

- [ ] Đang ở đúng branch.
- [ ] `git status` không có file ngoài phạm vi.
- [ ] Code build thành công.
- [ ] Tests pass.
- [ ] Simulation không có illegal action.
- [ ] Không có invariant failure.
- [ ] Không commit file `.exe`, debug artifact hoặc secret.
- [ ] Đã đọc toàn bộ tab **Files changed**.
- [ ] Base branch của PR là `main`.

