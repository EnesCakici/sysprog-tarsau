#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // Dosya bilgileri (stat), dizin olusturma (mkdir) ve izinler (chmod) icin
#include <sys/types.h>

#define MAX_FILES 32
#define MAX_SIZE (200 * 1024 * 1024) // Maksimum toplam dosya boyutu sınırı: 200 MB

/**
 * Bir dosyanin ASCII formatinda olup olmadigini ve boyutunu kontrol eder.
 * @param filename Kontrol edilecek dosyanin adi
 * @param out_file_size Dosyanin boyutunun yazilacagi degisken pointer'i
 * @return Dosya uyumlu ise 1, degilse veya dosya yoksa 0 döner.
 */
int check_file_compatibility(const char *filename, long *out_file_size) {
    struct stat st;
    
    // Dosya varligi ve sistem bilgilerinin kontrolü
    if (stat(filename, &st) != 0) {
        return 0; 
    }
    *out_file_size = st.st_size;

    // Dosyayi ikili (binary) modda okuyarak karakter kontrolü yapiyoruz
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }

    int ch;
    while ((ch = fgetc(file)) != EOF) {
        // Standart ASCII karakterleri 0-127 araligindadir (Karakter basina 1 bayt)
        if (ch < 0 || ch > 127) {
            fclose(file);
            return 0; // ASCII disi karakter tespiti (Uyumsuz dosya formatı)
        }
    }

    fclose(file);
    return 1; 
}

int main(int argc, char *argv[]) {
    // Argüman sayisi kontrolü: En az mod parametresi (-b veya -a) alinmalidir
    if (argc < 2) {
        fprintf(stderr, "Kullanim:\n");
        fprintf(stderr, "  Arsivleme: tarsau -b dosya1 dosya2 ... -o arsiv.sau\n");
        fprintf(stderr, "  Cikarma:   tarsau -a arsiv.sau [hedef_dizin]\n");
        return EXIT_FAILURE;
    }

    // =========================================================================
    // --- -b: ARŞİVLEME MODU ---
    // =========================================================================
    if (strcmp(argv[1], "-b") == 0) {
        char *input_files[MAX_FILES];
        int file_count = 0;
        char *output_file = "a.sau"; // Varsayilan cikis dosya adi

        // Komut satiri argümanlarinin dinamik olarak ayristirilmasi
        int i = 2;
        while (i < argc) {
            if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    output_file = argv[i + 1];
                    i += 2;
                    continue;
                } else {
                    fprintf(stderr, "Hata: -o parametresinden sonra dosya adi belirtilmedi.\n");
                    return EXIT_FAILURE;
                }
            }
            
            // Giris dosya listesinin kaydedilmesi
            if (file_count < MAX_FILES) {
                input_files[file_count++] = argv[i];
            } else {
                fprintf(stderr, "Hata: En fazla %d giris dosyasi belirtilebilir.\n", MAX_FILES);
                return EXIT_FAILURE;
            }
            i++;
        }

        // Tüm giris dosyalarinin format ve boyut yönünden dogrulanmasi
        long total_size = 0;
        for (int j = 0; j < file_count; j++) {
            long current_file_size = 0;
            if (!check_file_compatibility(input_files[j], &current_file_size)) {
                // Proje isterlerine uygun spesifik hata mesaji formatı
                printf("%s giris dosyasinin formatı uyumsuzdur!\n", input_files[j]);
                return EXIT_SUCCESS; 
            }
            total_size += current_file_size;
        }

        // Toplam boyut siniri kontrolü
        if (total_size > MAX_SIZE) {
            fprintf(stderr, "Hata: Giris dosyalarinin toplam boyutu 200 MB'i gecemez!\n");
            return EXIT_FAILURE;
        }

        // Organizasyon (Header) bölümünün dinamik olarak kurgulanmasi
        char header_buffer[10000] = ""; 
        struct stat st;

        for (int j = 0; j < file_count; j++) {
            if (stat(input_files[j], &st) == 0) {
                // Dosya izinlerinin oktal (taban 8) formatta maskelenmesi (Örn: 0644)
                int mode = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
                char temp[500];
                // |Dosya adi,izin,boyut| formatinda metadata string'i olusturma
                sprintf(temp, "|%s,%04o,%ld|", input_files[j], mode, st.st_size);
                strcat(header_buffer, temp);
            }
        }

        // İlk bölümün toplam boyutu: Kendi uzunlugu + boyut bilgisini tutan ilk 10 bayt
        long header_total_size = strlen(header_buffer) + 10;

        // Nihai arsiv dosyasinin yazma modunda acilmasi
        FILE *archive = fopen(output_file, "wb");
        if (!archive) {
            fprintf(stderr, "Hata: Arşiv dosyasi olusturulamadi.\n");
            return EXIT_FAILURE;
        }

        // İlk 10 bayta organizasyon bölümünün toplam boyutunun 10 karaktere tamamlanarak yazilmasi
        fprintf(archive, "%010ld", header_total_size);
        
        // Metadata kayitlarinin (Header) arsiğe basilmasi
        fwrite(header_buffer, sizeof(char), strlen(header_buffer), archive);

        // Giris dosyalarinin iceriklerinin sirayla veri bölümüne eklenmesi
        for (int j = 0; j < file_count; j++) {
            FILE *infile = fopen(input_files[j], "rb");
            if (infile) {
                int ch;
                while ((ch = fgetc(infile)) != EOF) {
                    fputc(ch, archive);
                }
                fclose(infile);
            }
        }

        fclose(archive);
        printf("Dosyalar birleştirildi.\n"); // Basarili birlesme mesaji
    } 
    // =========================================================================
    // --- -a: ARŞİVDEN ÇIKARMA MODU ---
    // =========================================================================
    else if (strcmp(argv[1], "-a") == 0) {
        // Parametre sayisi kontrolü: En az arsiv adi, en fazla hedef dizin alinabilir
        if (argc < 3 || argc > 4) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            return EXIT_SUCCESS;
        }

        char *archive_name = argv[2];
        char *target_dir = (argc == 4) ? argv[3] : NULL;

        // .sau uzantı kontrolü
        char *ext = strrchr(archive_name, '.');
        if (!ext || strcmp(ext, ".sau") != 0) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            return EXIT_SUCCESS;
        }

        FILE *archive = fopen(archive_name, "rb");
        if (!archive) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            return EXIT_SUCCESS;
        }

        // İlk 10 bayttan organizasyon alaninin boyutunun okunmasi
        char size_str[11] = {0};
        if (fread(size_str, 1, 10, archive) != 10) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            fclose(archive);
            return EXIT_SUCCESS;
        }

        long header_total_size = atol(size_str);
        long header_content_size = header_total_size - 10;

        if (header_content_size <= 0) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            fclose(archive);
            return EXIT_SUCCESS;
        }

        // Header verisinin dinamik bellege alinmasi
        char *header_content = malloc(header_content_size + 1);
        if (fread(header_content, 1, header_content_size, archive) != (size_t)header_content_size) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            free(header_content);
            fclose(archive);
            return EXIT_SUCCESS;
        }
        header_content[header_content_size] = '\0';

        // Hedef dizin parametresi varsa dizinin sistemde olusturulmasi
        if (target_dir != NULL) {
            mkdir(target_dir, 0777);
        }

        char decoded_names[MAX_FILES][256];
        int decoded_modes[MAX_FILES];
        long decoded_sizes[MAX_FILES];
        int file_count = 0;

        // Evrensel Ayrıştırma: strtok_r ile metadata bloklarinin parçalanmasi
        char *saveptr1;
        char *record = strtok_r(header_content, "|", &saveptr1);
        
        while (record != NULL && file_count < MAX_FILES) {
            if (strlen(record) > 0 && strchr(record, ',')) {
                char *saveptr2;
                char *name = strtok_r(record, ",", &saveptr2);
                char *mode_str = strtok_r(NULL, ",", &saveptr2);
                char *size_str_file = strtok_r(NULL, ",", &saveptr2);

                if (name && mode_str && size_str_file) {
                    strcpy(decoded_names[file_count], name);
                    // 8'lik tabandaki string mod verisinin tamsayiya cevrilmesi
                    decoded_modes[file_count] = (int)strtol(mode_str, NULL, 8);
                    decoded_sizes[file_count] = atol(size_str_file);
                    file_count++;
                }
            }
            record = strtok_r(NULL, "|", &saveptr1);
        }

        free(header_content);

        // Veri bölümünden dosyalarin okunup ayri ayri geri yazilmasi
        for (int j = 0; j < file_count; j++) {
            char final_path[512];
            if (target_dir != NULL) {
                sprintf(final_path, "%s/%s", target_dir, decoded_names[j]);
            } else {
                sprintf(final_path, "%s", decoded_names[j]);
            }

            FILE *outfile = fopen(final_path, "wb");
            if (!outfile) {
                fprintf(stderr, "Hata: Cikis dosyasi olusturulamadi: %s\n", final_path);
                fclose(archive);
                return EXIT_FAILURE;
            }

            // Dosya boyutu kadar baytin arsivden kopyalanmasi
            long bytes_to_read = decoded_sizes[j];
            for (long b = 0; b < bytes_to_read; b++) {
                int ch = fgetc(archive);
                if (ch == EOF) {
                    fprintf(stderr, "Hata: Arşiv verisi eksik veya bozuk.\n");
                    fclose(outfile);
                    fclose(archive);
                    return EXIT_FAILURE;
                }
                fputc(ch, outfile);
            }
            fclose(outfile);
            
            // Orijinal dosya izinlerinin (chmod) sisteme geri set edilmesi
            chmod(final_path, decoded_modes[j]);
        }

        fclose(archive);

        // Proje isterine uygun basarili cikis ekran çıktısı
        if (target_dir != NULL) {
            printf("%s dizininde ", target_dir);
        }
        for(int j = 0; j < file_count; j++) {
            printf("%s", decoded_names[j]);
            if (j < file_count - 1) printf(", ");
        }
        printf(" dosyaları açıldı.\n");
    } 
    else {
        fprintf(stderr, "Hata: Gecersiz parametre '%s'. Lutfen -b veya -a kullanin.\n", argv[1]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}