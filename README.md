# Báo cáo — Phát triển ứng dụng IoT

Repository này chứa **mã nguồn LaTeX** và tài nguyên phục vụ báo cáo bài tập lớn môn *Phát triển ứng dụng IoT* (ĐHQG-HCM — Đại học Bách Khoa). Mục tiêu là duy trì một kênh phát hành tài liệu **độc lập** với mã nguồn firmware (Receiver/Sender), giúp theo dõi phiên bản báo cáo và chia sẻ tài liệu mà không trộn lịch sử commit với embedded code.

---

## Nội dung chính

| Thành phần | Mô tả |
|------------|--------|
| `IOT_Application.tex` | File nguồn chính của báo cáo (Unicode, tiếng Việt). |
| `IOT_Application.pdf` | Kết quả biên dịch (tạo sau khi chạy XeLaTeX). |
| `assets/` | Logo, hình minh họa và ảnh thực nghiệm (đường dẫn được tham chiếu trong `.tex`). |
| `MISSING_IMAGES_NOTES.md` | Danh sách ảnh tùy chọn cần bổ sung nếu muốn thay placeholder. |
| `scripts/` | Script PowerShell hỗ trợ tạo/batch diagram (môi trường Windows). |

Các file phụ sinh khi biên dịch (`.aux`, `.log`, `.toc`, `.lof`, …) có thể được git-ignore tùy chính sách dự án; chúng không cần chỉnh sửa thủ công.

---

## Yêu cầu môi trường

Báo cáo được thiết kế cho **XeLaTeX** (`fontspec`, `unicode-math`, nội dung tiếng Việt). **Không** dùng `pdflatex` cho file này.

Sau khi cài bộ phân phối TeX phù hợp, xác nhận:

```bash
xelatex --version
```

### Cài đặt gợi ý theo hệ điều hành

- **macOS**  
  - [MacTeX](https://tug.org/mactex/) hoặc cài gói nhẹ hơn:  
    `brew install --cask mactex-no-gui`  
  - Bản đầy đủ GUI: `brew install --cask mactex`

- **Windows**  
  - [MiKTeX](https://miktex.org/download) hoặc [TeX Live](https://tug.org/texlive/)  
  - Đảm bảo thành phần **XeLaTeX** được cài đặt.

- **Linux (Debian/Ubuntu)**  

  ```bash
  sudo apt update
  sudo apt install texlive-xetex texlive-lang-other texlive-fonts-recommended
  ```

  Nếu thiếu gói cụ thể khi biên dịch, cài thêm `texlive-latex-extra` hoặc gói `texlive-full` (nặng hơn nhưng đầy đủ).

---

## Biên dịch và nhận file PDF

Tại thư mục chứa `IOT_Application.tex`:

```bash
xelatex -interaction=nonstopmode IOT_Application.tex
xelatex -interaction=nonstopmode IOT_Application.tex
```

Chạy **hai lần** để cập nhật mục lục, danh mục hình/bảng và tham chiếu nội bộ.

**Đầu ra:** `IOT_Application.pdf` được ghi **cùng thư mục** với file `.tex`. Trong quy trình LaTeX tiêu chuẩn, “xuất PDF” chính là bước biên dịch; không có bước export riêng.

**Tùy chọn — tự động lặp khi cần:**

```bash
latexmk -xelatex IOT_Application.tex
```

**VS Code / Cursor (LaTeX Workshop):** cấu hình recipe dùng engine **XeLaTeX**. PDF mặc định nằm cạnh `.tex` trừ khi bạn đặt `outDir` khác.

### Lưu trữ và chia sẻ

- Sao chép hoặc đổi tên `IOT_Application.pdf` tới vị trí cần nộp/lưu trữ.  
- Có thể dùng **File → Save As** (hoặc **Export as PDF**) trong trình đọc PDF nếu muốn tạo bản đặt tên riêng.

---

## Hình ảnh và placeholder

Một số hình tham chiếu đường dẫn trong `assets/`. Nếu file chưa có, báo cáo có thể hiển thị khung placeholder theo macro trong `.tex`. Chi tiết file ảnh gợi ý nằm trong `MISSING_IMAGES_NOTES.md`.

---

## Vai trò nhánh `main`

Nhánh `main` của repository này tập trung vào **tài liệu báo cáo**, tách biệt với lịch sử phát triển firmware Receiver/Sender, nhằm:

- quản lý phiên bản tài liệu rõ ràng;  
- cho phép clone/chỉnh sửa chỉ phần báo cáo;  
- giảm nhiễu giữa commit phần cứng/phần mềm nhúng và commit văn bản.

---

## Ghi chú

Tài liệu phục vụ mục đích học tập theo khung môn *Phát triển ứng dụng IoT*. Khi trích dẫn hoặc tái sử dụng, tuân thủ quy định của nhà trường và ghi rõ nguồn.
