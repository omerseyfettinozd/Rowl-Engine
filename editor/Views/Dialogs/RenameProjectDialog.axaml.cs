using Avalonia.Controls;

namespace RowlEngine.Editor.Views.Dialogs
{
    public partial class RenameProjectDialog : Window
    {
        public RenameProjectDialog() : this("") { }

        public RenameProjectDialog(string currentName)
        {
            InitializeComponent();

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
                        if (errorText != null) { errorText.Text = "Proje adı boş olamaz."; errorText.IsVisible = true; }
                        return;
                    }
                    Close(newName);
                };
            }
        }
    }
}
