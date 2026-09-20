using Avalonia.Controls;

namespace RowlEngine.Editor.Views.Dialogs
{
    public partial class RenameProjectDialog : Window
    {
        public RenameProjectDialog() : this("") { }

        public RenameProjectDialog(string currentName)
            : this(currentName, "Projeyi Yeniden Adlandır", "Yeni Proje Adı", "Proje adı boş olamaz.")
        {
        }

        /// <summary>
        /// Faz 4: genel metin-girdi varyantı (klasör oluşturma vb.). Başlık,
        /// alan etiketi ve boş-hata metni dışarıdan verilir.
        /// </summary>
        public RenameProjectDialog(string currentName, string title, string fieldLabel, string emptyError)
        {
            InitializeComponent();

            var titleBlock = this.FindControl<TextBlock>("TitleText");
            if (titleBlock != null) titleBlock.Text = title;

            var labelBlock = this.FindControl<TextBlock>("FieldLabelText");
            if (labelBlock != null) labelBlock.Text = fieldLabel;

            var nameBox = this.FindControl<TextBox>("NameBox");
            if (nameBox != null)
            {
                nameBox.Text = currentName;
                nameBox.SelectAll();
            }

            var cancelBtn = this.FindControl<Button>("CancelButton");
            if (cancelBtn != null) cancelBtn.Click += (_, _) => Close(null);

            var saveBtn = this.FindControl<Button>("SaveButton");
            var errorText = this.FindControl<TextBlock>("ErrorText");

            if (saveBtn != null)
            {
                saveBtn.Click += (_, _) =>
                {
                    string newName = nameBox?.Text?.Trim() ?? "";
                    if (string.IsNullOrWhiteSpace(newName))
                    {
                        if (errorText != null) { errorText.Text = emptyError; errorText.IsVisible = true; }
                        return;
                    }
                    Close(newName);
                };
            }
        }
    }
}
