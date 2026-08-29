using Avalonia.Controls;
using Avalonia.Media;

namespace RowlEngine.Editor.Views.Dialogs
{
    public partial class ConfirmDialog : Window
    {
        public ConfirmDialog() : this("Onay", "Bu işlemi onaylıyor musunuz?") { }

        public ConfirmDialog(string title, string message, string confirmText = "Evet", bool isDestructive = false)
        {
            InitializeComponent();

            var titleBlock = this.FindControl<TextBlock>("TitleText");
            if (titleBlock != null) titleBlock.Text = title;

            var msgBlock = this.FindControl<TextBlock>("MessageText");
            if (msgBlock != null) msgBlock.Text = message;

            var noBtn = this.FindControl<Button>("NoButton");
            if (noBtn != null) noBtn.Click += (_, _) => Close(false);

            var yesBtn = this.FindControl<Button>("YesButton");
            if (yesBtn != null)
            {
                yesBtn.Content = confirmText;
                if (isDestructive)
                {
                    yesBtn.Background = new SolidColorBrush(Color.Parse("#DC2626"));
                }
                yesBtn.Click += (_, _) => Close(true);
            }
        }
    }
}
