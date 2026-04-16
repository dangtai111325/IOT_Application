# Vaccine Cold Chain Monitoring (IoT) — Documentation Repository

## 1) Tổng quan về topic Vaccine Cold Chain Monitoring

Hệ thống Vaccine Cold Chain Monitoring tập trung vào việc giám sát điều kiện bảo quản vaccine theo thời gian thực, đặc biệt là nhiệt độ và độ ẩm trong suốt chuỗi lưu trữ/vận chuyển.

Mục tiêu kỹ thuật chính:

- Giám sát realtime nhiều điểm đo (multi-node).
- Cảnh báo sớm khi vượt ngưỡng rủi ro.
- Truy vết dữ liệu đầy đủ phục vụ audit/compliance.
- Hỗ trợ vận hành tập trung qua mô hình edge-gateway-cloud.

Kiến trúc trong tài liệu hiện tại dùng mô hình:

- `Sender` (edge node): thu thập dữ liệu cảm biến / mô phỏng dữ liệu.
- `Receiver` (gateway): tập trung trạng thái và điều phối cấu hình.
- `Cloud`: lưu trữ, quan sát và điều khiển từ xa.

![Minh họa hệ thống Vaccine Cold Chain Monitoring](assets/Gemini_Generated_Image_ofyee1ofyee1ofye.png)

---

## 2) Tổng quan repository

Repository này tập trung vào **tài liệu kỹ thuật bằng LaTeX** cho hệ thống, không phải nơi phát triển firmware đầy đủ.

### Thành phần chính

| Thành phần | Mô tả |
|------------|------|
| `IOT_Application.tex` | File nguồn LaTeX chính của tài liệu. |
| `IOT_Application.pdf` | File PDF sinh ra sau khi biên dịch từ `.tex`. |
| `assets/` | Hình ảnh, sơ đồ, logo, ảnh minh họa dùng trong tài liệu. |
| `scripts/` | Script hỗ trợ thao tác phụ trợ (ví dụ batch/generate). |
| `MISSING_IMAGES_NOTES.md` | Ghi chú các ảnh còn thiếu hoặc placeholder cần bổ sung. |

### Ghi chú file sinh tự động

Khi build LaTeX sẽ sinh thêm các file như:
`.aux`, `.log`, `.toc`, `.lof`, ...

Các file này là file trung gian, không chỉnh sửa thủ công.

---

## 3) Cài đặt XeLaTeX và build tài liệu

Tài liệu yêu cầu **XeLaTeX** (do dùng `fontspec`, `unicode-math`, nội dung tiếng Việt).  
Không dùng `pdflatex` cho file này.

### 3.1 Kiểm tra sau khi cài

```bash
xelatex --version
```

### 3.2 Cài đặt theo môi trường

#### macOS

- Khuyến nghị: [MacTeX](https://tug.org/mactex/)
- Hoặc dùng Homebrew:

```bash
brew install --cask mactex-no-gui
```

(Nếu cần đầy đủ GUI thì dùng `mactex`.)

#### Windows

- Cài một trong hai:
  - [MiKTeX](https://miktex.org/download)
  - [TeX Live](https://tug.org/texlive/)
- Đảm bảo thành phần `xelatex` đã được cài.

#### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install texlive-xetex texlive-lang-other texlive-fonts-recommended
```

Nếu thiếu package khi build, cài thêm:

```bash
sudo apt install texlive-latex-extra
```

### 3.3 Build PDF

Tại thư mục chứa `IOT_Application.tex`:

```bash
xelatex -interaction=nonstopmode IOT_Application.tex
xelatex -interaction=nonstopmode IOT_Application.tex
```

Chạy 2 lần để cập nhật mục lục, danh mục hình và cross-reference.

### 3.4 Build tự động (tùy chọn)

```bash
latexmk -xelatex IOT_Application.tex
```

### 3.5 Kết quả đầu ra

- File đầu ra: `IOT_Application.pdf`
- Vị trí mặc định: cùng thư mục với `IOT_Application.tex` (nếu không cấu hình `outDir` khác).
