
import rawpy
from PIL import Image
import numpy as np
import os
# import io <- Usunięte, nie jest już potrzebne

# --- KONFIGURACJA ---

# 1. Ścieżki
INPUT_FOLDER = 'C://Users/molec/OneDrive/Dokumenty/PlatformIO/Projects/lora-cam/NODE_C/badanie_1/content'
OUTPUT_FOLDER = '/content/output_v3'

# 2. Pliki DNG
FILES_TO_PROCESS = {
    '0001': 'widoczek', '0020': 'widoczek', '0026': 'tunel_ciemno',
    '0045': 'drzewa_dzien', '0077': 'zachmurzone', '0091': 'widok_zachmurzone',
    '0144': 'zmrok', '0230': 'ludzie', '0240': 'widok_dzien',
    '0497': 'widok_zmrok', '0506': 'zmrok', '0515': 'tlum_ludzi',
    '0725': 'nature', '0969': 'budynki_dzien', '1122': 'natura', '1259': 'natura',
}

# 3. Rozdzielczości
TARGET_RESOLUTIONS = {
    'QQVGA': (160, 120), 'HQVGA': (240, 176), 'QVGA':  (320, 240),
    'VGA':   (640, 480), 'SVGA':  (800, 600),
}

# 4. Poziomy jakości JPEG
JPEG_QUALITIES = [90, 75, 50, 30]

# --- GŁÓWNA CZĘŚĆ SKRYPTU ---

def process_images():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    print(f"Rozpoczynam przetwarzanie. Dane wyjściowe będą zapisywane w '{OUTPUT_FOLDER}'")

    for file_num, description in FILES_TO_PROCESS.items():
        input_path = os.path.join(INPUT_FOLDER, f'{file_num}.dng')

        if not os.path.exists(input_path):
            print(f"!!! Ostrzeżenie: Plik {input_path} nie został znaleziony. Pomijam.")
            continue

        print(f"\n--- Przetwarzam plik: {file_num}.dng ({description}) ---")

        try:
            with rawpy.imread(input_path) as raw:
                rgb_image = raw.postprocess(use_camera_wb=True, no_auto_bright=True)

            base_image = Image.fromarray(rgb_image)

            for res_name, res_size in TARGET_RESOLUTIONS.items():
                base_output_dir = os.path.join(OUTPUT_FOLDER, res_name, f"{file_num}_{description}")
                os.makedirs(base_output_dir, exist_ok=True)

                print(f"  -> Konwertuję do {res_name} ({res_size[0]}x{res_size[1]})")

                # Przygotuj obraz kolorowy i w skali szarości
                processed_image_color = base_image.resize(res_size, Image.Resampling.LANCZOS)
                processed_image_gray = processed_image_color.convert('L')

                # --- ZAPISYWANIE PLIKÓW ---

                # 1. JPEG (skala szarości) i (kolor) w różnych jakościach
                #    (Spełnia wymagania 1 i 2)
                for quality in JPEG_QUALITIES:
                    # Zapisz JPEG w skali szarości (czarnobiałe)
                    jpeg_gray_fn = os.path.join(base_output_dir, f"{file_num}_{res_name.lower()}_gray_q{quality}.jpeg")
                    processed_image_gray.save(jpeg_gray_fn, 'JPEG', quality=quality)

                    # Zapisz JPEG w kolorze
                    jpeg_color_fn = os.path.join(base_output_dir, f"{file_num}_{res_name.lower()}_color_q{quality}.jpeg")
                    processed_image_color.save(jpeg_color_fn, 'JPEG', quality=quality)

                # 2. Nieskompresowane BMP (kolor i skala szarości)
                #    (Spełnia wymagania 3 i 4)

                # Zapisz BMP w skali szarości (nieskompresowane czarnobiałe)
                bmp_gray_fn = os.path.join(base_output_dir, f"{file_num}_{res_name.lower()}_gray.bmp")
                processed_image_gray.save(bmp_gray_fn, 'BMP')

                # Zapisz BMP w kolorze (nieskompresowane kolorowe)
                bmp_color_fn = os.path.join(base_output_dir, f"{file_num}_{res_name.lower()}_color.bmp")
                processed_image_color.save(bmp_color_fn, 'BMP')

                # Sekcja 8 (generowanie .jraw) została usunięta zgodnie z wymaganiami

        except Exception as e:
            print(f"!!! Wystąpił błąd podczas przetwarzania pliku {file_num}.dng: {e}")

    print("\n--- Zakończono przetwarzanie wszystkich plików! ---")

# Uruchom skrypt przetwarzający
process_images()